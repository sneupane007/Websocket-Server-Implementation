#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h> // for write()
#include "websocket_util.h"
#include "server_util.h"
#include <thread>


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

    std::cout << "Server listening on port 8082..." << std::endl;

    while (true) {
        // 6. Accept a Connection
        // This blocks here until a client (like a browser) connects.
        int new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) {
            perror("accept");
            continue;
        }
        std::cout << "New connection accepted! Spawning thread..." << std::endl;
        std::thread(handle_client, new_socket).detach();
        
    }

    // 10. Close the master listener (not reached in this loop)
    close(server_fd);
    return 0;
}