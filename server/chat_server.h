#ifndef CHAT_SERVER_H
#define CHAT_SERVER_H

#include <string>
#include "websocket_handler.h"
#include "ClientManager.h"
#include "../database/DatabaseManager.hpp"

class ChatServer : public WebSocketHandler {
private:
    ClientManager&   clients;
    DatabaseManager& db;

    // Low-level WebSocket helpers
    static std::string base64_encode(const unsigned char* data, size_t len);
    void perform_handshake(int socket, const std::string& client_key);

    // The blocking message loop — runs for the lifetime of one WebSocket connection
    void enter_message_loop(int socket, ClientRole role, const std::string& id);

public:
    ChatServer(ClientManager& clients, DatabaseManager& db);

    // WebSocketHandler interface
    std::string path_prefix() const override { return "/chat"; }
    bool matches(const std::string& path) const override {
        return path == "/chat" || path == "/admin/chat";
    }

    // Called by the HttpRouter when it detects a WebSocket Upgrade.
    // The router hands the socket here and steps away completely.
    void accept_connection(int socket, const std::string& path, const std::string& query, const std::string& ws_key) override;

    // Encode and send a single text frame to a socket
    static void send_frame(int socket, const std::string& message);
};

#endif

