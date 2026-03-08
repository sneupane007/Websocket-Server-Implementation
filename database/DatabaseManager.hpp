#pragma once
#include <pqxx/pqxx>
#include <mutex>
#include <string>
#include <iostream>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

using namespace nlohmann;

class DatabaseManager {
private:
    std::mutex db_mutex;
    std::unique_ptr<pqxx::connection> conn;

public:
    DatabaseManager(const std::string& conn_str) {
        try {
            conn = std::make_unique<pqxx::connection>(conn_str);
            if (conn->is_open()) {
                std::cout << "Connected to AWS SQL server: " << conn->dbname() << std::endl;
                create_tables();
            }
        } catch (const std::exception &e) {
            std::cerr << "DB connection error: " << e.what() << std::endl;
        }
    }

    void create_tables() {
        std::lock_guard<std::mutex> lock(db_mutex);
        if (!conn) return;
        try {
            pqxx::work W(*conn);
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
        std::lock_guard<std::mutex> lock(db_mutex);
        if (!conn) return;
        
        try {
            pqxx::work W(*conn);
            // $1 = receiver, $2 = sender, $3 = message
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
    
    // Returns JSON array of message objects
    json fetch_chat_history(const std::string& visitor_id) {
        std::lock_guard<std::mutex> lock(db_mutex);
        json j = json::array();
        if (!conn) return j;

        try {
            pqxx::work W(*conn);
            // Use exec_params for safety
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
        std::lock_guard<std::mutex> lock(db_mutex);
        std::vector<std::string> visitors;
        if (!conn) return visitors;

        try {
            pqxx::work W(*conn);
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