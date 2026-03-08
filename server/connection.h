#pragma once

#include <string>
#include <deque>
#include "ClientManager.h"

// Forward declaration — full type defined in websocket_handler.h
class WebSocketHandler;

// ─────────────────────────────────────────────
// ParseState — where a connection is in its lifecycle
// ─────────────────────────────────────────────
enum class ParseState {
    READING_HTTP_HEADERS,  // accumulating bytes until "\r\n\r\n"
    WS_CONNECTED,          // WebSocket handshake done, reading frames
    CLOSING                // draining writes, about to close
};

// ─────────────────────────────────────────────
// ConnectionState — all per-fd state owned by the Reactor
// ─────────────────────────────────────────────
struct ConnectionState {
    int        fd;
    ParseState state = ParseState::READING_HTTP_HEADERS;

    // I/O buffers
    std::string             read_buf;    // raw bytes not yet consumed
    std::deque<std::string> write_queue; // WS frames waiting to be sent

    // Set after WebSocket upgrade
    ClientRole        role;
    std::string       id;
    std::string       ws_key;       // Sec-WebSocket-Key from upgrade request
    WebSocketHandler* ws_handler = nullptr; // handler that owns this connection
    bool              ws_handshake_done = false;
};
