#include "chat_server.h"
#include "reactor.h"

#include <iostream>
#include <vector>
#include <set>
#include <cstdint>
#include <unistd.h>
#include <thread>
#include <openssl/sha.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

ChatServer::ChatServer(ClientManager& clients, DatabaseManager& db)
    : clients(clients), db(db) {}

// ─────────────────────────────────────────────
// base64_encode
// ─────────────────────────────────────────────
std::string ChatServer::base64_encode(const unsigned char* data, size_t len) {
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string res;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (data[i] << 16)
                     | ((i + 1 < len ? data[i + 1] : 0) << 8)
                     | (i + 2 < len ? data[i + 2] : 0);
        res += alphabet[(val >> 18) & 0x3F];
        res += alphabet[(val >> 12) & 0x3F];
        res += (i + 1 < len) ? alphabet[(val >> 6) & 0x3F] : '=';
        res += (i + 2 < len) ? alphabet[val & 0x3F]        : '=';
    }
    return res;
}

// ─────────────────────────────────────────────
// perform_handshake — sends the HTTP 101 response
// Direct write is safe: ~100 bytes, always fits in the kernel send buffer.
// ─────────────────────────────────────────────
void ChatServer::perform_handshake(int fd, const std::string& client_key) {
    const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = client_key + magic;

    unsigned char hash[20];
    SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()),
         combined.length(), hash);

    std::string accept_key = base64_encode(hash, 20);

    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key + "\r\n\r\n";

    write(fd, response.c_str(), response.length());
    std::cout << "[ChatServer] Handshake complete for fd=" << fd << std::endl;
}

// ─────────────────────────────────────────────
// build_frame — encode a string as an unmasked WS text frame (server side)
// ─────────────────────────────────────────────
std::string ChatServer::build_frame(const std::string& message) {
    std::string frame;
    frame.push_back('\x81'); // FIN + TEXT opcode

    size_t len = message.size();
    if (len < 126) {
        frame.push_back(static_cast<char>(len));
    } else if (len <= 65535) {
        frame.push_back('\x7E');
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    } else {
        frame.push_back('\x7F');
        for (int i = 7; i >= 0; i--)
            frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
    }
    frame.append(message);
    return frame;
}

// ─────────────────────────────────────────────
// on_open — called by Reactor when WS upgrade completes
// ─────────────────────────────────────────────
void ChatServer::on_open(ConnectionState& conn,
                          const std::string& path, const std::string& query) {
    // Determine role from path
    if (path == "/admin/chat") {
        conn.role = ClientRole::ADMIN;
        conn.id   = "admin";
    } else {
        conn.role = ClientRole::VISITOR;
        size_t name_pos = query.find("name=");
        if (name_pos != std::string::npos) {
            conn.id = query.substr(name_pos + 5);
            size_t amp = conn.id.find('&');
            if (amp != std::string::npos) conn.id = conn.id.substr(0, amp);
        }
        if (conn.id.empty()) conn.id = "visitor_" + std::to_string(conn.fd);
    }

    std::cout << "[ChatServer] on_open: " << conn.id
              << " path=" << path << std::endl;

    // Send HTTP 101 (direct write — safe, tiny one-time response)
    perform_handshake(conn.fd, conn.ws_key);
    conn.ws_handshake_done = true;

    // Register in ClientManager
    clients.add_client(conn.fd, conn.role, conn.id);

    if (conn.role == ClientRole::ADMIN) {
        // DB snapshot is async — avoid blocking the reactor thread
        int target_fd = conn.fd;
        std::thread([this, target_fd]() {
            auto active = clients.get_active_visitors();
            std::set<std::string> active_set(active.begin(), active.end());
            auto db_visitors = db.fetch_unique_visitors();

            json snapshot;
            snapshot["type"]     = "snapshot";
            snapshot["visitors"] = json::array();

            for (const auto& name : active) {
                json v; v["name"] = name; v["online"] = true;
                snapshot["visitors"].push_back(v);
            }
            for (const auto& name : db_visitors) {
                if (!active_set.count(name)) {
                    json v; v["name"] = name; v["online"] = false;
                    snapshot["visitors"].push_back(v);
                }
            }
            // Deliver result via pipe — wakes the reactor thread safely
            reactor->post_result(target_fd, build_frame(snapshot.dump()));
        }).detach();

    } else {
        // Notify admins immediately (pure in-memory, microseconds)
        json evt;
        evt["type"] = "connect";
        evt["user"] = conn.id;
        std::string frame = build_frame(evt.dump());
        for (int admin_sock : clients.get_admin_sockets()) {
            reactor->queue_write(admin_sock, frame);
        }
    }
}

// ─────────────────────────────────────────────
// on_message — called by Reactor for each decoded WS frame
// ─────────────────────────────────────────────
void ChatServer::on_message(ConnectionState& conn, const std::string& payload) {
    std::cout << "[ChatServer] " << conn.id << ": " << payload << std::endl;

    if (conn.role == ClientRole::VISITOR) {
        // Immediately forward to all admins
        json envelope;
        envelope["type"]    = "message";
        envelope["from"]    = conn.id;
        envelope["message"] = payload;
        std::string frame = build_frame(envelope.dump());
        for (int admin_sock : clients.get_admin_sockets()) {
            reactor->queue_write(admin_sock, frame);
        }

        // Async DB persist — fire and forget
        std::string sender = conn.id;
        std::thread([this, sender, payload]() {
            db.insert_message(sender, "admin", payload);
        }).detach();

    } else if (conn.role == ClientRole::ADMIN) {
        try {
            json j          = json::parse(payload);
            std::string target  = j.value("receiver", "");
            std::string content = j.value("message",  "");

            if (target.empty() || content.empty()) {
                reactor->queue_write(conn.fd,
                    build_frame(R"({"error":"Missing receiver or message"})"));
                return;
            }

            int target_sock = clients.get_visitor_socket(target);
            if (target_sock != -1) {
                json reply;
                reply["from"]    = "admin";
                reply["message"] = content;
                reactor->queue_write(target_sock, build_frame(reply.dump()));
            } else {
                json err;
                err["error"] = "User " + target + " is not connected.";
                reactor->queue_write(conn.fd, build_frame(err.dump()));
            }

            // Async DB persist
            std::thread([this, target, content]() {
                db.insert_message("admin", target, content);
            }).detach();

        } catch (const json::parse_error&) {
            reactor->queue_write(conn.fd,
                build_frame(R"({"error":"Invalid JSON"})"));
        }
    }
}

// ─────────────────────────────────────────────
// on_close — called by Reactor when connection is being torn down
// ─────────────────────────────────────────────
void ChatServer::on_close(ConnectionState& conn) {
    std::cout << "[ChatServer] on_close: " << conn.id << std::endl;

    if (conn.role == ClientRole::VISITOR && !conn.id.empty()) {
        json evt;
        evt["type"] = "disconnect";
        evt["user"] = conn.id;
        std::string frame = build_frame(evt.dump());
        for (int admin_sock : clients.get_admin_sockets()) {
            reactor->queue_write(admin_sock, frame);
        }
    }

    clients.remove_client(conn.fd);
}
