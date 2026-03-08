#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "websocket_handler.h"
#include "http_handler.h"
#include "connection.h"

// ─────────────────────────────────────────────
// HttpRouter — parses pre-buffered HTTP headers and dispatches
//
//   route_buffered() is called by the Reactor once "\r\n\r\n"
//   has been seen in the read buffer.  No read() call inside.
// ─────────────────────────────────────────────
class HttpRouter {
public:
    HttpRouter() = default;

    // Register handlers — order matters for HTTP handlers (first match wins)
    void register_ws_handler(WebSocketHandler* handler);
    void register_http_handler(HttpHandler* handler);

    // Called by Reactor::dispatch_http() with already-buffered header bytes.
    // Returns true  → WebSocket upgrade; on_open() called, connection persists.
    // Returns false → HTTP request handled; caller should close the connection.
    bool route_buffered(ConnectionState& conn, const std::string& headers_raw);

private:
    std::vector<WebSocketHandler*> ws_handlers;
    std::vector<HttpHandler*>      http_handlers;

    std::unordered_map<std::string, std::string>
        parse_headers(const std::string& raw);

    bool is_websocket_upgrade(
        const std::unordered_map<std::string, std::string>& headers);
};
