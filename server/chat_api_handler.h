#pragma once
#include "http_handler.h"
#include "../database/DatabaseManager.hpp"
#include "ClientManager.h"

// ─────────────────────────────────────────────
// ChatApiHandler — HTTP endpoints for the chat feature
//
//   GET /api/visitors  → JSON list of visitors w/ online status
//   GET /api/history   → chat history for a visitor
// ─────────────────────────────────────────────
class ChatApiHandler : public HttpHandler {
private:
    DatabaseManager& db;
    ClientManager&   clients;

    void handle_get_visitors(int socket);
    void handle_get_history(int socket, const std::string& query);
    void send_json(int socket, const std::string& body);

public:
    ChatApiHandler(DatabaseManager& db, ClientManager& clients);

    std::string path_prefix() const override { return "/api/"; }
    bool handle(int socket,
            const std::string& method, const std::string& path,
            const std::string& query,
            const std::unordered_map<std::string, std::string>& headers) override;
};
