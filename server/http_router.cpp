#include "http_router.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <unistd.h>

// ─────────────────────────────────────────────
// Helper: lowercase a string for case-insensitive comparisons
// ─────────────────────────────────────────────
static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// ─────────────────────────────────────────────
// Handler registration
// ─────────────────────────────────────────────
void HttpRouter::register_ws_handler(WebSocketHandler* handler) {
    ws_handlers.push_back(handler);
    std::cout << "[Router] Registered WebSocket handler: "
              << handler->path_prefix() << std::endl;
}

void HttpRouter::register_http_handler(HttpHandler* handler) {
    http_handlers.push_back(handler);
    std::cout << "[Router] Registered HTTP handler: "
              << handler->path_prefix() << std::endl;
}

// ─────────────────────────────────────────────
// parse_headers — key/value map from raw HTTP text
// Skips the first line (request line) and parses subsequent header lines.
// ─────────────────────────────────────────────
std::unordered_map<std::string, std::string>
HttpRouter::parse_headers(const std::string& raw) {
    std::unordered_map<std::string, std::string> headers;

    size_t pos = raw.find("\r\n");
    if (pos == std::string::npos) return headers;

    size_t start = pos + 2;
    while ((pos = raw.find("\r\n", start)) != std::string::npos) {
        std::string line = raw.substr(start, pos - start);
        if (line.empty()) break;

        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key   = line.substr(0, colon);
            std::string value = line.substr(colon + 2); // skip ": "
            headers[key] = value;
        }
        start = pos + 2;
    }
    return headers;
}

// ─────────────────────────────────────────────
// is_websocket_upgrade
// ─────────────────────────────────────────────
bool HttpRouter::is_websocket_upgrade(
        const std::unordered_map<std::string, std::string>& headers) {
    bool has_upgrade    = false;
    bool has_connection = false;
    bool has_key        = headers.count("Sec-WebSocket-Key") > 0;

    auto it = headers.find("Upgrade");
    if (it != headers.end() && to_lower(it->second) == "websocket")
        has_upgrade = true;

    it = headers.find("Connection");
    if (it != headers.end() &&
        to_lower(it->second).find("upgrade") != std::string::npos)
        has_connection = true;

    return has_upgrade && has_connection && has_key;
}

// ─────────────────────────────────────────────
// route_buffered — dispatch using pre-read header bytes
//
//   Returns true  → WebSocket upgrade; on_open() was called.
//   Returns false → HTTP handled (or 404); caller should close the fd.
// ─────────────────────────────────────────────
bool HttpRouter::route_buffered(ConnectionState& conn,
                                const std::string& headers_raw) {
    // Parse the request line
    std::istringstream ss(headers_raw);
    std::string method, full_path;
    ss >> method >> full_path;

    std::string path  = full_path;
    std::string query;
    size_t qmark = full_path.find('?');
    if (qmark != std::string::npos) {
        path  = full_path.substr(0, qmark);
        query = full_path.substr(qmark + 1);
    }

    std::cout << "[Router] " << method << " " << path << std::endl;

    auto headers = parse_headers(headers_raw);

    // WebSocket upgrade?
    if (is_websocket_upgrade(headers)) {
        conn.ws_key = headers["Sec-WebSocket-Key"];

        for (auto* handler : ws_handlers) {
            if (handler->matches(path)) {
                conn.ws_handler = handler;
                handler->on_open(conn, path, query);
                return true; // WS upgrade complete
            }
        }

        // No matching WS handler
        std::cerr << "[Router] No WebSocket handler for: " << path << std::endl;
        const char* err = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        write(conn.fd, err, __builtin_strlen(err));
        return false;
    }

    // HTTP request — iterate handlers, first match wins
    for (auto* handler : http_handlers) {
        if (handler->handle(conn.fd, method, path, query, headers)) {
            return false;
        }
    }

    // 404
    const std::string r =
        "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    write(conn.fd, r.c_str(), r.length());
    return false;
}
