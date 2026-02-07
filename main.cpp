#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>

#include "websocket_util.h"




int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};

    // 1. Create Socket File Descriptor
    // AF_INET = IPv4, SOCK_STREAM = TCP
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 2. Set Socket Options
    // This allows us to restart the server immediately without "Address already in use" errors.
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("error in setsockopt");
        exit(EXIT_FAILURE);
    }


    // 3. Define Address and Port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on any network interface (localhost, wifi, etc.)
    address.sin_port = htons(8082);       // Host To Network Short (Endianness conversion)

    // 4. Bind the socket to the port
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 5. Start Listening
    // 3 is the "backlog"—the number of pending connections the OS will queue.
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server listening on port 8080..." << std::endl;

    while (true) {
        // 6. Accept a Connection
        // This blocks here until a client (like a browser) connects.
        if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

         // 7. Read Data
        // We move bytes from the kernel buffer into our 'buffer' array.
        ssize_t valread = read(new_socket, buffer, 1024);
        if (valread > 0) {
            std::cout << "--- Received Request ---" << std::endl;
            std::cout << buffer << std::endl;
            std::cout << "------------------------" << std::endl;

            // 8. Send a basic HTTP response (so the browser doesn't hang)
           
        }
        if(is_websocket(buffer)){
            perform_handshake( new_socket, parse_headers(buffer)["Sec-WebSocket-Key"]);
            // After handshake, enter WebSocket mode (this is a simplified version)

            enter_websocket_mode(new_socket);
        }
        else{
            const char* hello = "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: 12\n\nHello World!";
            write(new_socket, hello, strlen(hello));
            close(new_socket);
        }
        
        // 9. Close the client socket
        close(new_socket);
        
        // Clear buffer for the next request
        memset(buffer, 0, 1024);
    }

    // 10. Close the master listener (not reached in this loop)
    close(server_fd);
    return 0;
}