#pragma once

#include <string>
#include "connection.h"

// ─────────────────────────────────────────────
// WebSocketHandler — event-driven interface for WebSocket features
//
//   The Reactor calls on_open/on_message/on_close as events arrive.
//   No blocking — handlers must return quickly.
// ─────────────────────────────────────────────
class WebSocketHandler {
public:
    virtual ~WebSocketHandler() = default;

    // URL path this handler owns, e.g. "/chat"
    virtual std::string path_prefix() const = 0;

    // Does this handler own the given request path?
    virtual bool matches(const std::string& path) const {
        return path.find(path_prefix()) == 0;
    }

    // Called once after the WebSocket upgrade is detected.
    // conn.ws_key holds Sec-WebSocket-Key; handler must send the 101 response.
    virtual void on_open(ConnectionState& conn,
                         const std::string& path,
                         const std::string& query) = 0;

    // Called for each fully-decoded WebSocket frame payload.
    virtual void on_message(ConnectionState& conn,
                            const std::string& payload) = 0;

    // Called when the connection closes (client disconnect or Close frame).
    virtual void on_close(ConnectionState& conn) = 0;
};
