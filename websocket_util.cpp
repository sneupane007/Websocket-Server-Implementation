#include "websocket_util.h"
#include <iostream>
#include <openssl/sha.h>
#include <unistd.h> // for write()

#include <algorithm>
#include <unordered_map>
#include <string>

std::string to_lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}



// This converts the raw buffer into a searchable Map
std::unordered_map<std::string, std::string> parse_headers(char* buffer) {
    std::unordered_map<std::string, std::string> headers;
    std::string raw(buffer);
    size_t pos = 0;
    
    // Find the first line (Request Line) and skip it
    pos = raw.find("\r\n");
    if (pos == std::string::npos) return headers;
    std::string line;
    size_t start = pos + 2;

    // Iterate through lines until we hit the empty line (\r\n\r\n)
    while ((pos = raw.find("\r\n", start)) != std::string::npos) {
        line = raw.substr(start, pos - start);
        if (line.empty()) break; // End of headers

        size_t colon = line.find(":");
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 2); // +2 to skip ": "
            headers[key] = value;
        }
        start = pos + 2;
    }
    return headers;
}

bool is_websocket(char* buffer) {

    // Pass the pointer to the underlying array1
    auto headers = parse_headers(buffer);
    
    // Check for the "Trinity" of WebSocket headers
    bool has_upgrade = false;
    bool has_connection = false;
    bool has_key = headers.count("Sec-WebSocket-Key");
    std::cout << "[Layer 2] Analyzing Headers.n 1.." << std::endl;
    if (headers.count("Upgrade") && to_lower_str(headers["Upgrade"]) == "websocket") {
        has_upgrade = true;
    }

    if (headers.count("Connection") && to_lower_str(headers["Connection"]).find("upgrade") != std::string::npos) {
        has_connection = true;
    }
    std::cout << "has_upgrade: " << has_upgrade << ", has_connection: " << has_connection << ", has_key: " << has_key << std::endl;

    return has_upgrade && has_connection && has_key;
}



std::string base64_encode(const unsigned char* data, size_t len) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string res;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (data[i] << 16) | ((i + 1 < len ? data[i + 1] : 0) << 8) | (i + 2 < len ? data[i + 2] : 0);
        res += alphabet[(val >> 18) & 0x3F];
        res += alphabet[(val >> 12) & 0x3F];
        res += (i + 1 < len) ? alphabet[(val >> 6) & 0x3F] : '=';
        res += (i + 2 < len) ? alphabet[val & 0x3F] : '=';
    }
    return res;
}



void enter_websocket_mode(int client_socket) {
    unsigned char buffer[1024];
    
    while (true) {
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer));
        if (bytes_read <= 0) break;

        // 1. Check Opcode (Byte 0)
        // 0x81 means "Final fragment" + "Text frame"
        if (buffer[0] == 0x88) {
            std::cout << "Client sent Close frame. Hanging up." << std::endl;
            break;
        }

        // 2. Get Payload Length (Byte 1)
        // We mask out the first bit (the "Mask" flag) using bitwise AND
        int payload_len = buffer[1] & 0x7F;

        // 3. Extract the 4-byte Masking Key (Bytes 2, 3, 4, 5)
        unsigned char mask[4];
        mask[0] = buffer[2];
        mask[1] = buffer[3];
        mask[2] = buffer[4];
        mask[3] = buffer[5];

        // 4. Unmask the Data (Starts at Byte 6)
        std::string message = "";
        for (int i = 0; i < payload_len; i++) {
            // Unmask by XORing with the mask key in a 4-byte cycle
            message += buffer[6 + i] ^ mask[i % 4];
        }

        std::cout << "🔓 Decoded Message: " << message << std::endl;
    }
    close(client_socket);
}

void perform_handshake( int client_socket, const std::string& client_key) {
    std::cout << "[Layer 3] Starting Handshake Math..." << std::endl;

    // 1. Concatenate key with the Magic GUID
    std::string magic_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = client_key + magic_guid;

    // 2. Calculate SHA-1 Hash
    unsigned char hash[20]; // SHA-1 always produces 20 bytes
    SHA1((const unsigned char*)combined.c_str(), combined.length(), hash);

    // 3. Encode the Hash in Base64
    std::string accept_key = base64_encode(hash, 20);

    // 4. Formulate the HTTP 101 Response
    // IMPORTANT: Note the \r\n line endings. HTTP requires them.
    std::string response = 
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key + "\r\n\r\n";

    write(client_socket, response.c_str(), response.length());
    std::cout << "[Layer 3] Handshake Sent. Accept Key: " << accept_key << std::endl;
}