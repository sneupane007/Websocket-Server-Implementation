#pragma once
#include <pqxx/pqxx>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <memory>
#include <string>

class ConnectionPool {
    std::vector<std::unique_ptr<pqxx::connection>> connections;
    std::queue<pqxx::connection*> available;
    std::mutex mutex;
    std::condition_variable cv;

    pqxx::connection* acquire() {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return !available.empty(); });
        pqxx::connection* conn = available.front();
        available.pop();
        return conn;
    }

    void release(pqxx::connection* conn) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            available.push(conn);
        }
        cv.notify_one();
    }

public:
    ConnectionPool(const std::string& conn_str, size_t pool_size) {
        for (size_t i = 0; i < pool_size; ++i) {
            connections.push_back(std::make_unique<pqxx::connection>(conn_str));
            available.push(connections.back().get());
        }
    }

    // RAII guard: acquires on construct, releases on destruct
    struct Guard {
        pqxx::connection* conn;
        ConnectionPool& pool;

        Guard(ConnectionPool& p) : pool(p), conn(p.acquire()) {}
        ~Guard() { pool.release(conn); }
        pqxx::connection& get() { return *conn; }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
    };
};
