#include "rest_controller.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <set>
#include <unistd.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

RestController::RestController(DatabaseManager& db, ClientManager& clients)
    : db(db), clients(clients) {}

// ─────────────────────────────────────────────
// build_file_response — reads a file and wraps it in HTTP 200
// ─────────────────────────────────────────────
std::string RestController::build_file_response(
    const std::string& filepath, const std::string& mime_type) {
    std::ifstream file(filepath);
    if (!file.is_open()) return "";

    std::stringstream buf;
    buf << file.rdbuf();
    std::string content = buf.str();

    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: " + mime_type + "\r\n"
           "Content-Length: " + std::to_string(content.length()) + "\r\n"
           "Connection: close\r\n\r\n" + content;
}

// ─────────────────────────────────────────────
// serve_file — serves any static file (HTML, CSS, JS, …)
// ─────────────────────────────────────────────
void RestController::serve_file(int socket,
        const std::string& filepath, const std::string& mime_type) {
    std::string response = build_file_response(filepath, mime_type);
    if (response.empty()) {
        send_404(socket);
        return;
    }
    write(socket, response.c_str(), response.length());
}

// ─────────────────────────────────────────────
// handle_get_visitors — GET /api/visitors
//   Returns a JSON array of visitor objects with online status.
//   Merges currently-connected visitors with past visitors from DB.
// ─────────────────────────────────────────────
void RestController::handle_get_visitors(int socket) {
    // Active visitors (currently connected)
    auto active = clients.get_active_visitors();
    std::set<std::string> active_set(active.begin(), active.end());

    // Past visitors from DB
    auto db_visitors = db.fetch_unique_visitors();

    // Merge: start with active, then add any DB-only visitors
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

    std::string body = j.dump();

    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.length()) + "\r\n"
        "Connection: close\r\n\r\n" + body;

    write(socket, response.c_str(), response.length());
}

// ─────────────────────────────────────────────
// handle_get_history — GET /api/history?visitor=<id>
//   Asks the DatabaseManager for past messages and returns JSON.
// ─────────────────────────────────────────────
void RestController::handle_get_history(int socket, const std::string& path) {
    // Simple query-string parsing:  /api/history?visitor=visitor_42
    std::string visitor_id;
    size_t q = path.find("visitor=");
    if (q != std::string::npos) {
        visitor_id = path.substr(q + 8);
        // Strip anything after & if present
        size_t amp = visitor_id.find('&');
        if (amp != std::string::npos) visitor_id = visitor_id.substr(0, amp);
    }

    json history = db.fetch_chat_history(visitor_id);
    std::string body = history.dump();

    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.length()) + "\r\n"
        "Connection: close\r\n\r\n" + body;

    write(socket, response.c_str(), response.length());
}

// ─────────────────────────────────────────────
// send_403
// ─────────────────────────────────────────────
void RestController::send_403(int socket) {
    std::string r = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    write(socket, r.c_str(), r.length());
}

// ─────────────────────────────────────────────
// send_404
// ─────────────────────────────────────────────
void RestController::send_404(int socket) {
    std::string r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    write(socket, r.c_str(), r.length());
}
