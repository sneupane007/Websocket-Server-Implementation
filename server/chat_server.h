#pragma once

#include <string>
#include "websocket_handler.h"
#include "ClientManager.h"
#include "../database/DatabaseManager.hpp"

// Forward declare Reactor to avoid circular include
class Reactor;

class ChatServer : public WebSocketHandler {
private:
    ClientManager&   clients;
    DatabaseManager& db;
    Reactor*         reactor = nullptr;

    // Low-level WebSocket helpers
    static std::string base64_encode(const unsigned char* data, size_t len);
    void perform_handshake(int fd, const std::string& client_key);

public:
    ChatServer(ClientManager& clients, DatabaseManager& db);

    // Must be called before reactor.run()
    void set_reactor(Reactor* r) { reactor = r; }

    // WebSocketHandler interface
    std::string path_prefix() const override { return "/chat"; }
    bool matches(const std::string& path) const override {
        return path == "/chat" || path == "/admin/chat";
    }

    void on_open(ConnectionState& conn,
                 const std::string& path, const std::string& query) override;
    void on_message(ConnectionState& conn, const std::string& payload) override;
    void on_close(ConnectionState& conn) override;

    // Build an unmasked WS text frame (server→client direction)
    static std::string build_frame(const std::string& message);
};
