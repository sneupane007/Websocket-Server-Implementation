#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include "server_util.h"
#include "websocket_util.h"

std::string read_file(const std::string& filename, const std::string& mime_type) {
    std::ifstream file(filename);
    if (!file.is_open()) return "HTTP/1.1 404 Not Found\r\n\r\n";
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: " + mime_type + "\r\n"
           "Content-Length: " + std::to_string(content.length()) + "\r\n"
           "Connection: close\r\n\r\n" + content;
}


void handle_client(int client_socket) {
    char buffer[1024] = {0};
    ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    
    if (bytes_read <= 0) {
        close(client_socket);
        return;
    }

    // 1. Check if it's a WebSocket request BEFORE sending anything else
    if (is_websocket(buffer)) {
        auto headers = parse_headers(buffer);
        if(headers.count("Sec-WebSocket-Key")) {
            perform_handshake(client_socket, headers["Sec-WebSocket-Key"]);
            enter_websocket_mode(client_socket); // Stays in loop until disconnect
            return; // Thread finishes here after user leaves
        }
    } 

    // 2. If it wasn't a WebSocket, check for CSS
    std::string request(buffer);
    if (request.find("GET /style.css") != std::string::npos) {
        std::string css = read_file("resources/style.css", "text/css");
        write(client_socket, css.c_str(), css.length());
    } 
    // 3. Otherwise, serve HTML
    else {
        std::string html = read_file("resources/index.html", "text/html");
        write(client_socket, html.c_str(), html.length());
    }

    // 4. For non-WebSocket requests, we close the socket immediately
    close(client_socket);
}