#pragma once
#include <string>

// ─────────────────────────────────────────────
// WebSocketHandler — interface for WebSocket features
//
//   Any feature that needs a persistent WebSocket connection
//   implements this.  The HttpRouter detects WS upgrades and
//   dispatches to the handler whose path_prefix() matches.
// ─────────────────────────────────────────────
class WebSocketHandler {
public:
    virtual ~WebSocketHandler() = default;

    // The URL path this handler owns, e.g. "/chat", "/ws/presence"
    virtual std::string path_prefix() const = 0;

    // Does this handler match the given request path?
    // Default: prefix match.  Override for complex routing (e.g. multiple paths).
    virtual bool matches(const std::string& path) const {
        return path.find(path_prefix()) == 0;
    }

    // Called by the router after the WS upgrade is detected.
    // The handler is responsible for the handshake and message loop.
    // This call blocks for the lifetime of the connection.
    virtual void accept_connection(int socket,
            const std::string& path, const std::string& query,
            const std::string& ws_key) = 0;
};
