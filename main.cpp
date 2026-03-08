#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>

#include "server/ClientManager.h"
#include "database/DatabaseManager.hpp"
#include "server/chat_server.h"
#include "server/chat_api_handler.h"
#include "server/static_handler.h"
#include "server/presence_handler.h"
#include "server/reactor.h"

// ─────────────────────────────────────────────
// Simple .env file loader
// ─────────────────────────────────────────────
static std::string load_env(const std::string& key, const std::string& default_val = "") {
    std::ifstream file(".env");
    std::string line;
    while (std::getline(file, line)) {
        if (line.find(key + "=") == 0)
            return line.substr(key.length() + 1);
    }
    return default_val;
}

int main() {
    // ── 1. Load Config ──────────────────────────
    std::string db_host = load_env("DB_HOST");
    std::string db_port = load_env("DB_PORT");
    std::string db_name = load_env("DB_NAME");
    std::string db_user = load_env("DB_USER");
    std::string db_pass = load_env("DB_PASS");

    size_t db_pool_size = 4;
    std::string dp_env = load_env("DB_POOL_SIZE");
    if (!dp_env.empty()) db_pool_size = std::stoul(dp_env);

    std::string conn_str = "host=" + db_host +
        " port=" + db_port +
        " dbname=" + db_name +
        " user=" + db_user +
        " password=" + db_pass;

    // ── 2. Create shared managers ───────────────
    ClientManager   client_manager;
    DatabaseManager db_manager(conn_str, db_pool_size);

    // ── 3. Create feature handlers ──────────────
    ChatServer      chat_handler(client_manager, db_manager);
    ChatApiHandler  api_handler(db_manager, client_manager);
    StaticHandler   static_handler("../portfolio copy/dist");
    PresenceHandler presence_handler;

    // ── 4. Socket setup ─────────────────────────
    int server_fd;
    struct sockaddr_in address{};
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        return 1;
    }
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(8082);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return 1;
    }
    // Large backlog to handle burst accepts
    if (listen(server_fd, 1024) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "Server listening on port 8082 (db_pool="
              << db_pool_size << ", reactor model)..." << std::endl;

    // ── 5. Reactor — replaces ThreadPool + accept loop ──────────────
    Reactor reactor(server_fd, client_manager, db_manager);

    // Give handlers a back-reference for queue_write / post_result
    chat_handler.set_reactor(&reactor);
    presence_handler.set_reactor(&reactor);

    // Register handlers (WS first, then HTTP in match-priority order)
    reactor.register_ws_handler(&presence_handler); // /ws/presence
    reactor.register_ws_handler(&chat_handler);     // /chat, /admin/chat
    reactor.register_http_handler(&api_handler);    // /api/*
    reactor.register_http_handler(&static_handler); // / catch-all (last)

    // ── 6. Run forever ──────────────────────────
    reactor.run();

    close(server_fd);
    return 0;
}
