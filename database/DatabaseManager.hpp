#include <pqxx/pqxx>
#include <mutex>
#include <string>
#include <vector>

class DatabaseManager {
private:
    std::mutex db_mutex;
    // The connection object from libpqxx
    std::unique_ptr<pqxx::connection> conn;

public:
    DatabaseManager() {
        try {
            // Replace with your actual DB credentials
            conn = std::make_unique<pqxx::connection>(
                "host=localhost dbname=chat_db user=postgres password=yourpassword"
            );
            if (conn->is_open()) {
                std::cout << "✅ Connected to PostgreSQL: " << conn->dbname() << std::endl;
                create_tables();
            }
        } catch (const std::exception &e) {
            std::cerr << "❌ DB Error: " << e.what() << std::endl;
        }
    }

    void create_tables() {
        pqxx::work W(*conn);
        W.exec(
            "CREATE TABLE IF NOT EXISTS chat_history ("
            "id SERIAL PRIMARY KEY,"
            "username TEXT,"
            "message TEXT,"
            "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP);"
        );
        W.commit();
    }

    void save_message(const std::string& user, const std::string& msg) {
        std::lock_guard<std::mutex> lock(db_mutex); // Thread safety!
        try {
            pqxx::work W(*conn);
            W.exec_params(
                "INSERT INTO chat_history (username, message) VALUES ($1, $2)",
                user, msg
            );
            W.commit();
        } catch (const std::exception &e) {
            std::cerr << "Save Error: " << e.what() << std::endl;
        }
    }
};