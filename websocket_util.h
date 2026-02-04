#ifndef WEBSOCKET_UTIL_H
#define WEBSOCKET_UTIL_H

#include <string>
#include <iostream>
// Declarations
std::string base64_encode(const unsigned char* data, size_t len);

// Note: I added parameters for client_key and client_socket 
// so the function isn't relying on global variables.
void perform_handshake(int client_socket, const std::string& client_key);

void enter_websocket_mode(int client_socket);


std::string to_lower_str(std::string s);

std::unordered_map<std::string, std::string> parse_headers(char* buffer);

bool is_websocket(char* buffer);

#endif