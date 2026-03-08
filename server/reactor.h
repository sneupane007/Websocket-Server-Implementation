#pragma once

#include <unordered_map>
#include <queue>
#include <mutex>
#include <string>

#include "poller.h"
#include "connection.h"
#include "http_router.h"
#include "ClientManager.h"
#include "../database/DatabaseManager.hpp"

// ─────────────────────────────────────────────
// DbResult — a pre-encoded WS frame to deliver to a connection.
// Produced by DB threads, consumed by the reactor thread via notify pipe.
// ─────────────────────────────────────────────
struct DbResult {
    int         target_fd;
    std::string frame; // already WS-frame-encoded bytes
};

// ─────────────────────────────────────────────
// Reactor — single-threaded event loop
//
//   Owns all connections, the platform poller, and the DB notify pipe.
//   DB threads post results via post_result(); the pipe wakes epoll/kqueue.
// ─────────────────────────────────────────────
class Reactor {
public:
    Reactor(int server_fd, ClientManager& clients, DatabaseManager& db);
    ~Reactor();

    // Register handlers before calling run()
    void register_ws_handler(WebSocketHandler* handler);
    void register_http_handler(HttpHandler* handler);

    // Blocks forever — the entire server lives here
    void run();

    // Thread-safe: called by DB worker threads to deliver WS frames
    void post_result(int target_fd, std::string frame);

    // Queue a WS frame for sending; registers EPOLLOUT/EVFILT_WRITE if needed.
    // Must be called from the reactor thread (or from on_open/on_message/on_close).
    void queue_write(int fd, const std::string& data);

private:
    int    server_fd_;
    Poller poller_;
    std::unordered_map<int, ConnectionState> conns_;

    // DB notification pipe
    int notify_read_fd_  = -1;
    int notify_write_fd_ = -1;

    // Cross-thread queue (DB results)
    std::mutex           result_mutex_;
    std::queue<DbResult> pending_results_;

    // Handler lists (owned by the router)
    HttpRouter router_;

    // Shared app state
    ClientManager&   clients_;
    DatabaseManager& db_;

    // Event handlers
    void on_accept();
    void on_readable(int fd);
    void on_writable(int fd);
    void on_db_notify();

    // Connection management
    void close_conn(int fd);
    void try_flush(int fd);

    // Dispatch
    void dispatch_http(ConnectionState& conn, const std::string& headers_raw);
    void dispatch_ws_frame(ConnectionState& conn, const std::string& payload);
};
