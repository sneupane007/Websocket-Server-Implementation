#include <pqxx/pqxx>
#include <mutex>
#include <string>
#include <iostream>
#include <memory>

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
        pqxx::work W(*conn);
        try { W.exec(
                "CREATE TABLE IF NOT EXISTS chat_history ("
                "id SERIAL PRIMARY KEY,"
                "reciever_id TEXT NOT NULL,"
                "username TEXT,"
                "message TEXT,"
                "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP);"
            );
            W.commit();
        } catch (const std::exception &e) {
            std::cerr << "Database error during table creation: " << e.what() << std::endl;
        }
    }

   void insert_message(const std::string& sender, const std::string& receiver, const std::string& message) {
        // Use lock_guard for thread safety when accessing the database connection
        std::lock_guard<std::mutex> lock(db_mutex); 
        
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
};