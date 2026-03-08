#include "chat_api_handler.h"
#include <sstream>
#include <set>
#include <unistd.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

ChatApiHandler::ChatApiHandler(DatabaseManager& db, ClientManager& clients)
    : db(db), clients(clients) {}

// ─────────────────────────────────────────────
// send_json — wraps a JSON string in an HTTP 200 response
// ─────────────────────────────────────────────
void ChatApiHandler::send_json(int socket, const std::string& body) {
    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.length()) + "\r\n"
        "Connection: close\r\n\r\n" + body;
    write(socket, response.c_str(), response.length());
}

// ─────────────────────────────────────────────
// handle_get_visitors — GET /api/visitors
// ─────────────────────────────────────────────
void ChatApiHandler::handle_get_visitors(int socket) {
    auto active = clients.get_active_visitors();
    std::set<std::string> active_set(active.begin(), active.end());
    auto db_visitors = db.fetch_unique_visitors();

    json j = json::array();
    for (auto& name : active) {
        json v;
        v["name"]   = name;
        v["online"] = true;
        j.push_back(v);
    }
    for (auto& name : db_visitors) {
        if (!active_set.count(name)) {
            json v;
            v["name"]   = name;
            v["online"] = false;
            j.push_back(v);
        }
    }
    send_json(socket, j.dump());
}

// ─────────────────────────────────────────────
// handle_get_history — GET /api/history?visitor=<id>
// ─────────────────────────────────────────────
void ChatApiHandler::handle_get_history(int socket, const std::string& query) {
    std::string visitor_id;
    size_t q = query.find("visitor=");
    if (q != std::string::npos) {
        visitor_id = query.substr(q + 8);
        size_t amp = visitor_id.find('&');
        if (amp != std::string::npos) visitor_id = visitor_id.substr(0, amp);
    }

    json history = db.fetch_chat_history(visitor_id);
    send_json(socket, history.dump());
}

// ─────────────────────────────────────────────
// handle — route /api/* requests
// ─────────────────────────────────────────────
bool ChatApiHandler::handle(int socket,
        const std::string& method, const std::string& path,
        const std::string& query,
        const std::unordered_map<std::string, std::string>& /*headers*/) {
    if (method != "GET") return false;

    if (path == "/api/visitors") {
        handle_get_visitors(socket);
        return true;
    }
    if (path == "/api/history") {
        handle_get_history(socket, query);
        return true;
    }
    return false; // Unknown /api/ path — pass to next handler
}
