#include "chat_server.h"

#include <iostream>
#include <vector>
#include <set>
#include <cstdint>
#include <unistd.h>
#include <openssl/sha.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

ChatServer::ChatServer(ClientManager& clients, DatabaseManager& db)
    : clients(clients), db(db) {}

// ─────────────────────────────────────────────
// base64_encode — needed for the WebSocket accept-key math
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
// perform_handshake — completes the HTTP 101 upgrade
// ─────────────────────────────────────────────
void ChatServer::perform_handshake(int socket, const std::string& client_key) {
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

    write(socket, response.c_str(), response.length());
    std::cout << "[ChatServer] Handshake complete. Accept-Key: "
              << accept_key << std::endl;
}

// ─────────────────────────────────────────────
// send_frame — encode a string into a WebSocket text frame and write it
// ─────────────────────────────────────────────
void ChatServer::send_frame(int socket, const std::string& message) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + TEXT opcode

    size_t len = message.length();
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--)
            frame.push_back((len >> (i * 8)) & 0xFF);
    }

    frame.insert(frame.end(), message.begin(), message.end());
    write(socket, frame.data(), frame.size());
}

// ─────────────────────────────────────────────
// accept_connection — called by the HttpRouter
//
//   The router has already verified the WebSocket headers.
//   We do the handshake, assign a role based on path, and enter
//   the blocking message loop.  The calling thread is ours now.
// ─────────────────────────────────────────────
void ChatServer::accept_connection(int socket,
        const std::string& path, const std::string& query, const std::string& ws_key) {

    // 1. Determine role from path
    ClientRole role;
    std::string id;
    if (path == "/admin/chat") {
        role = ClientRole::ADMIN;
        id   = "admin";
    } else {
        role = ClientRole::VISITOR;
        // Extract name from query string:  name=John
        size_t name_pos = query.find("name=");
        if (name_pos != std::string::npos) {
            id = query.substr(name_pos + 5);
            // Strip anything after & if present
            size_t amp = id.find('&');
            if (amp != std::string::npos) id = id.substr(0, amp);
        }
        if (id.empty()) {
            id = "visitor_" + std::to_string(socket);
        }
    }

    std::cout << "[ChatServer] Accepting " << id
              << " on path " << path << std::endl;

    // 2. Complete the handshake
    perform_handshake(socket, ws_key);

    // 3. Enter the message loop (blocks until disconnect)
    enter_message_loop(socket, role, id);
}

// ─────────────────────────────────────────────
// enter_message_loop — the blocking read loop
//
//   Decodes WebSocket frames, persists messages to the DB,
//   and routes them to the right recipient(s).
// ─────────────────────────────────────────────
void ChatServer::enter_message_loop(int socket,
        ClientRole role, const std::string& id) {

    clients.add_client(socket, role, id);

    // When an admin connects, send a snapshot of all visitors with online status
    if (role == ClientRole::ADMIN) {
        auto active = clients.get_active_visitors();
        std::set<std::string> active_set(active.begin(), active.end());
        auto db_visitors = db.fetch_unique_visitors();

        json snapshot;
        snapshot["type"] = "snapshot";
        snapshot["visitors"] = json::array();

        for (auto& name : active) {
            json v;
            v["name"]   = name;
            v["online"] = true;
            snapshot["visitors"].push_back(v);
        }
        for (auto& name : db_visitors) {
            if (!active_set.count(name)) {
                json v;
                v["name"]   = name;
                v["online"] = false;
                snapshot["visitors"].push_back(v);
            }
        }
        send_frame(socket, snapshot.dump());
    }

    // Notify admins that a visitor came online
    if (role == ClientRole::VISITOR) {
        json evt;
        evt["type"] = "connect";
        evt["user"] = id;
        std::string payload = evt.dump();
        for (int admin_sock : clients.get_admin_sockets()) {
            send_frame(admin_sock, payload);
        }
    }

    unsigned char buffer[4096];

    while (true) {
        ssize_t bytes_read = read(socket, buffer, sizeof(buffer));
        if (bytes_read <= 0) break;

        // Close frame
        if (buffer[0] == 0x88) {
            std::cout << "[ChatServer] " << id << " sent Close frame." << std::endl;
            break;
        }

        // --- Decode the WebSocket frame ---
        int payload_len = buffer[1] & 0x7F;
        int mask_offset = 2;

        size_t actual_len = payload_len;
        if (payload_len == 126) {
            actual_len  = (buffer[2] << 8) | buffer[3];
            mask_offset = 4;
        } else if (payload_len == 127) {
            mask_offset = 10;
            // simplified — chat messages are never this large
        }

        unsigned char mask[4];
        for (int i = 0; i < 4; i++) mask[i] = buffer[mask_offset + i];

        int data_offset = mask_offset + 4;
        std::string message;
        for (size_t i = 0; i < actual_len; i++) {
            if (data_offset + (int)i < (int)sizeof(buffer))
                message += buffer[data_offset + i] ^ mask[i % 4];
        }

        std::cout << "[ChatServer] " << id << ": " << message << std::endl;

        // --- Route the message based on role ---
        if (role == ClientRole::VISITOR) {
            // Persist visitor → admin
            db.insert_message(id, "admin", message);

            // Forward to every connected admin
            auto admin_socks = clients.get_admin_sockets();
            // Build a small JSON envelope so the admin dashboard can parse it
            json envelope;
            envelope["type"]    = "message";
            envelope["from"]    = id;
            envelope["message"] = message;
            std::string payload = envelope.dump();

            for (int admin_sock : admin_socks) {
                send_frame(admin_sock, payload);
            }
        }
        else if (role == ClientRole::ADMIN) {
            // Admin sends JSON:  { "receiver": "visitor_42", "message": "hi" }
            try {
                json j = json::parse(message);
                std::string target  = j.value("receiver", "");
                std::string content = j.value("message", "");

                if (target.empty() || content.empty()) {
                    send_frame(socket, "{\"error\":\"Missing receiver or message\"}");
                    continue;
                }

                db.insert_message("admin", target, content);

                int target_sock = clients.get_visitor_socket(target);
                if (target_sock != -1) {
                    json reply;
                    reply["from"]    = "admin";
                    reply["message"] = content;
                    send_frame(target_sock, reply.dump());
                } else {
                    json err;
                    err["error"] = "User " + target + " is not connected.";
                    send_frame(socket, err.dump());
                }
            } catch (const json::parse_error&) {
                send_frame(socket, "{\"error\":\"Invalid JSON\"}");
            }
        }
    }

    // Notify admins that a visitor went offline
    if (role == ClientRole::VISITOR) {
        json evt;
        evt["type"] = "disconnect";
        evt["user"] = id;
        std::string payload = evt.dump();
        for (int admin_sock : clients.get_admin_sockets()) {
            send_frame(admin_sock, payload);
        }
    }

    clients.remove_client(socket);
    close(socket);
}
