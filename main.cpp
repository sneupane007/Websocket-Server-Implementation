#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <string>

#include "server/ClientManager.h"
#include "database/DatabaseManager.hpp"
#include "server/chat_server.h"
#include "server/chat_api_handler.h"
#include "server/static_handler.h"
#include "server/http_router.h"

// ─────────────────────────────────────────────
// Simple .env file loader
// ─────────────────────────────────────────────
static std::string load_env(const std::string& key) {
    std::ifstream file(".env");
    std::string line;
    while (std::getline(file, line)) {
        if (line.find(key + "=") == 0)
            return line.substr(key.length() + 1);
    }
    return "";
}

int main() {
    // ── 1. Load Config ──────────────────────────
    std::string db_host = load_env("DB_HOST");
    std::string db_port = load_env("DB_PORT");
    std::string db_name = load_env("DB_NAME");
    std::string db_user = load_env("DB_USER");
    std::string db_pass = load_env("DB_PASS");

    std::string conn_str = "host=" + db_host +
        " port=" + db_port +
        " dbname=" + db_name +
        " user=" + db_user +
        " password=" + db_pass;

    // ── 2. Create the shared managers ───────────
    ClientManager   client_manager;
    DatabaseManager db_manager(conn_str);

    // ── 3. Create feature handlers ──────────────
    ChatServer      chat_handler(client_manager, db_manager);
    ChatApiHandler  api_handler(db_manager, client_manager);
    StaticHandler   static_handler("../portfolio copy/dist");

    // ── 4. Wire up the router ───────────────────
    HttpRouter router;

    // WebSocket handlers
    router.register_ws_handler(&chat_handler);

    // HTTP handlers (order matters — first match wins)
    router.register_http_handler(&api_handler);       // /api/*
    router.register_http_handler(&static_handler);    // / (catch-all, must be last)

    // ── 5. Socket setup ─────────────────────────
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(8082);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server listening on port 8082..." << std::endl;

    // ── 6. Accept loop ──────────────────────────
    while (true) {
        int new_socket = accept(server_fd, (struct sockaddr*)&address,
                                (socklen_t*)&addrlen);
        if (new_socket < 0) { perror("accept"); continue; }

        std::cout << "New connection accepted." << std::endl;
        std::thread([&router, new_socket]() {
            router.route(new_socket);
        }).detach();
    }

    close(server_fd);
    return 0;
}