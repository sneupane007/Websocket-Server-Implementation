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
    std::cout << "[Router] Registered WebSocket handler: " << handler->path_prefix() << std::endl;
}

void HttpRouter::register_http_handler(HttpHandler* handler) {
    http_handlers.push_back(handler);
    std::cout << "[Router] Registered HTTP handler: " << handler->path_prefix() << std::endl;
}

// ─────────────────────────────────────────────
// parse_headers — turns raw HTTP bytes into a key/value map
// ─────────────────────────────────────────────
std::unordered_map<std::string, std::string>
HttpRouter::parse_headers(const char* buffer) {
    std::unordered_map<std::string, std::string> headers;
    std::string raw(buffer);
    size_t pos = raw.find("\r\n");
    if (pos == std::string::npos) return headers;

    size_t start = pos + 2;
    while ((pos = raw.find("\r\n", start)) != std::string::npos) {
        std::string line = raw.substr(start, pos - start);
        if (line.empty()) break;

        size_t colon = line.find(":");
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
// is_websocket_upgrade — checks the "trinity" of WebSocket headers
// ─────────────────────────────────────────────
bool HttpRouter::is_websocket_upgrade(
        const std::unordered_map<std::string, std::string>& headers) {
    bool has_upgrade    = false;
    bool has_connection = false;
    bool has_key        = headers.count("Sec-WebSocket-Key");

    auto it = headers.find("Upgrade");
    if (it != headers.end() && to_lower(it->second) == "websocket")
        has_upgrade = true;

    it = headers.find("Connection");
    if (it != headers.end() && to_lower(it->second).find("upgrade") != std::string::npos)
        has_connection = true;

    return has_upgrade && has_connection && has_key;
}

// ─────────────────────────────────────────────
//  route() — THE FRONT DOOR
//
//  Reads raw bytes once.  Decides:
//    • WebSocket?  → find matching WS handler by path prefix
//    • HTTP?       → iterate HTTP handlers; first match wins
// ─────────────────────────────────────────────
void HttpRouter::route(int client_socket) {
    char buffer[4096] = {0};
    ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);

    if (bytes_read <= 0) {
        close(client_socket);
        return;
    }

    // --- 1. Parse the request line  --------------------------------
    std::string request(buffer);
    std::istringstream ss(request);
    std::string method, full_path;
    ss >> method >> full_path;

    // Split path from query string
    std::string path = full_path;
    std::string query;
    size_t qmark = full_path.find('?');
    if (qmark != std::string::npos) {
        path  = full_path.substr(0, qmark);
        query = full_path.substr(qmark + 1);
    }

    std::cout << "[Router] " << method << " " << path << std::endl;

    // --- 2. Parse headers ------------------------------------------
    auto headers = parse_headers(buffer);

    // --- 3. WebSocket upgrade? → dispatch to matching WS handler ---
    if (is_websocket_upgrade(headers)) {
        std::string ws_key = headers["Sec-WebSocket-Key"];

        for (auto* handler : ws_handlers) {
            if (handler->matches(path)) {
                handler->accept_connection(client_socket, path, query, ws_key);
                return;  // Thread exits after WS session ends
            }
        }

        // No handler matched — close
        std::cerr << "[Router] No WebSocket handler for path: " << path << std::endl;
        close(client_socket);
        return;
    }

    // --- 4. HTTP request → iterate handlers, first match wins ------
    for (auto* handler : http_handlers) {
        if (handler->handle(client_socket, method, path, query, headers)) {
            close(client_socket);
            return;
        }
    }

    // No handler matched — 404
    std::string r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    write(client_socket, r.c_str(), r.length());
    close(client_socket);
}
