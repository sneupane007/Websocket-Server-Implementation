#pragma once

// ─────────────────────────────────────────────
// ws_util.h — shared WebSocket utilities
//
// Provides inline helpers for:
//   ws::handshake()   — send HTTP 101 upgrade response
//   ws::build_frame() — encode a string as an unmasked WS text frame
//
// Included by both ChatServer and PresenceHandler to avoid duplication.
// ─────────────────────────────────────────────

#include <string>
#include <cstdint>
#include <unistd.h>
#include <openssl/sha.h>

namespace ws {

// base64_encode — internal helper
inline std::string base64_encode(const unsigned char* data, size_t len) {
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string res;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (data[i] << 16)
                     | ((i + 1 < len ? data[i + 1] : 0) << 8)
                     | (i + 2 < len ? data[i + 2] : 0);
        res += alphabet[(val >> 18) & 0x3F];
        res += alphabet[(val >> 12) & 0x3F];
        res += (i + 1 < len) ? alphabet[(val >> 6) & 0x3F] : '=';
        res += (i + 2 < len) ? alphabet[val & 0x3F]        : '=';
    }
    return res;
}

// handshake — send HTTP 101 + Sec-WebSocket-Accept header
// Direct write is safe: ~100 bytes, always fits in the kernel send buffer.
inline void handshake(int fd, const std::string& ws_key) {
    const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = ws_key + magic;

    unsigned char hash[20];
    SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()),
         combined.length(), hash);

    std::string accept_key = base64_encode(hash, 20);

    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key + "\r\n\r\n";

    write(fd, response.c_str(), response.length());
}

// build_frame — encode a string as an unmasked WS text frame (server → client)
inline std::string build_frame(const std::string& message) {
    std::string frame;
    frame.push_back('\x81'); // FIN + TEXT opcode

    size_t len = message.size();
    if (len < 126) {
        frame.push_back(static_cast<char>(len));
    } else if (len <= 65535) {
        frame.push_back('\x7E');
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    } else {
        frame.push_back('\x7F');
        for (int i = 7; i >= 0; i--)
            frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
    }
    frame.append(message);
    return frame;
}

} // namespace ws
