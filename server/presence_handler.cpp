#include "presence_handler.h"
#include "ws_util.h"
#include "reactor.h"

#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ─────────────────────────────────────────────
// on_open — called by Reactor when WS upgrade is detected for /ws/presence
// ─────────────────────────────────────────────
void PresenceHandler::on_open(ConnectionState& conn,
                               const std::string& path, const std::string& query) {
    ws::handshake(conn.fd, conn.ws_key);
    conn.ws_handshake_done = true;

    presence_fds.insert(conn.fd);
    std::cout << "[PresenceHandler] on_open fd=" << conn.fd
              << " count=" << presence_fds.size() << std::endl;

    broadcast(static_cast<int>(presence_fds.size()));
}

// ─────────────────────────────────────────────
// on_message — no-op: this endpoint is server-push only
// ─────────────────────────────────────────────
void PresenceHandler::on_message(ConnectionState& conn, const std::string& payload) {
    // Server-push only; ignore any client frames
    (void)conn;
    (void)payload;
}

// ─────────────────────────────────────────────
// on_close — called by Reactor when connection closes
// ─────────────────────────────────────────────
void PresenceHandler::on_close(ConnectionState& conn) {
    presence_fds.erase(conn.fd);
    std::cout << "[PresenceHandler] on_close fd=" << conn.fd
              << " count=" << presence_fds.size() << std::endl;

    broadcast(static_cast<int>(presence_fds.size()));
}

// ─────────────────────────────────────────────
// broadcast — push current count to all presence subscribers
// ─────────────────────────────────────────────
void PresenceHandler::broadcast(int count) {
    json j;
    j["type"]  = "presence";
    j["count"] = count;
    std::string frame = ws::build_frame(j.dump());
    for (int fd : presence_fds) {
        reactor->queue_write(fd, frame);
    }
}
