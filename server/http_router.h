#ifndef HTTP_ROUTER_H
#define HTTP_ROUTER_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include "websocket_handler.h"
#include "http_handler.h"

// ─────────────────────────────────────────────
// HttpRouter — the front door
//
//   Reads raw bytes from a socket, parses the request,
//   and dispatches to the appropriate registered handler.
//
//   Features register themselves at startup via
//   register_ws_handler() and register_http_handler().
// ─────────────────────────────────────────────
class HttpRouter {
private:
    std::vector<WebSocketHandler*>  ws_handlers;
    std::vector<HttpHandler*>       http_handlers;

    // Internal helpers for parsing raw HTTP bytes
    std::unordered_map<std::string, std::string> parse_headers(const char* buffer);
    bool is_websocket_upgrade(const std::unordered_map<std::string, std::string>& headers);

public:
    HttpRouter() = default;

    // Register handlers — order matters for HTTP handlers (first match wins)
    void register_ws_handler(WebSocketHandler* handler);
    void register_http_handler(HttpHandler* handler);

    // The single entry point. Called by main() for every new connection.
    void route(int client_socket);
};

#endif
