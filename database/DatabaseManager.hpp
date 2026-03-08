#pragma once
#include <pqxx/pqxx>
#include <string>
#include <iostream>
#include <vector>
#include <nlohmann/json.hpp>
#include "connection_pool.h"

using namespace nlohmann;

class DatabaseManager {
    ConnectionPool pool;

public:
    DatabaseManager(const std::string& conn_str, size_t pool_size = 5)
        : pool(conn_str, pool_size)
    {
        std::cout << "Connected to database with pool size " << pool_size << std::endl;
        create_tables();
    }

    void create_tables() {
        ConnectionPool::Guard g(pool);
        try {
            pqxx::work W(g.get());
            W.exec(
                "CREATE TABLE IF NOT EXISTS chat_history ("
                "id SERIAL PRIMARY KEY,"
                "receiver_id TEXT NOT NULL,"
                "username TEXT,"
                "message TEXT,"
                "sent_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP);"
            );
            W.commit();
        } catch (const std::exception &e) {
            std::cerr << "Database error during table creation: " << e.what() << std::endl;
        }
    }

    void insert_message(const std::string& sender, const std::string& receiver, const std::string& message) {
        ConnectionPool::Guard g(pool);
        try {
            pqxx::work W(g.get());
            W.exec(
                "INSERT INTO chat_history (receiver_id, username, message) VALUES ($1, $2, $3)",
                pqxx::params{receiver, sender, message}
            );
            W.commit();
            std::cout << "Message saved to AWS RDS." << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "Database error during insert: " << e.what() << std::endl;
        }
    }

    json fetch_chat_history(const std::string& visitor_id) {
        ConnectionPool::Guard g(pool);
        json j = json::array();
        try {
            pqxx::work W(g.get());
            std::string query = "SELECT username, receiver_id, message, sent_at FROM chat_history "
                                "WHERE username = $1 OR receiver_id = $1 "
                                "ORDER BY sent_at ASC;";
            pqxx::result R = W.exec(query, pqxx::params{visitor_id});
            for (auto row : R) {
                json msg;
                msg["sender"] = row["username"].c_str();
                msg["receiver"] = row["receiver_id"].c_str();
                msg["message"] = row["message"].c_str();
                msg["timestamp"] = row["sent_at"].c_str();
                j.push_back(msg);
            }
        } catch (const std::exception &e) {
            std::cerr << "Error fetching history: " << e.what() << std::endl;
        }
        return j;
    }

    std::vector<std::string> fetch_unique_visitors() {
        ConnectionPool::Guard g(pool);
        std::vector<std::string> visitors;
        try {
            pqxx::work W(g.get());
            pqxx::result R = W.exec("SELECT DISTINCT username FROM chat_history WHERE username != 'admin';");
            for (auto row : R) {
                visitors.push_back(row[0].c_str());
            }
        } catch (const std::exception &e) {
            std::cerr << "Error fetching visitors: " << e.what() << std::endl;
        }
        return visitors;
    }
};
