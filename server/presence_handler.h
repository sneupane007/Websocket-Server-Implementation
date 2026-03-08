#pragma once

#include <set>
#include <string>
#include "websocket_handler.h"

// Forward declaration — avoids circular include with reactor.h
class Reactor;

// ─────────────────────────────────────────────
// PresenceHandler — live visitor count via WebSocket
//
// Browsers connect to /ws/presence on page load.
// Server pushes {"type":"presence","count":N} whenever anyone joins or leaves.
// No DB needed — count lives in memory.
// No messages from client expected (server-push only).
// ─────────────────────────────────────────────
class PresenceHandler : public WebSocketHandler {
    std::set<int> presence_fds;  // reactor-thread-only; no mutex needed
    Reactor*      reactor = nullptr;

    void broadcast(int count);   // sends {"type":"presence","count":N} to all

public:
    void set_reactor(Reactor* r) { reactor = r; }

    std::string path_prefix() const override { return "/ws/presence"; }

    void on_open(ConnectionState& conn,
                 const std::string& path, const std::string& query) override;
    void on_message(ConnectionState& conn,
                    const std::string& payload) override; // no-op
    void on_close(ConnectionState& conn) override;
};
