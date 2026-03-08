#include "chat_server.h"
#include "reactor.h"
#include "ws_util.h"

#include <iostream>
#include <vector>
#include <set>
#include <thread>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

ChatServer::ChatServer(ClientManager& clients, DatabaseManager& db)
    : clients(clients), db(db) {}

// ─────────────────────────────────────────────
// perform_handshake — delegates to ws::handshake
// ─────────────────────────────────────────────
void ChatServer::perform_handshake(int fd, const std::string& client_key) {
    ws::handshake(fd, client_key);
    std::cout << "[ChatServer] Handshake complete for fd=" << fd << std::endl;
}

// ─────────────────────────────────────────────
// build_frame — public static wrapper around ws::build_frame (API compat)
// ─────────────────────────────────────────────
std::string ChatServer::build_frame(const std::string& message) {
    return ws::build_frame(message);
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
