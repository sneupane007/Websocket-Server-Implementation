#include <cstdlib>
#include <iostream>
#include <fstream>   // for std::ifstream
#include <sstream>   // for string stream parsing
#include <string>
#include <memory>    // for std::unique_ptr and std::make_unique
#include <pqxx/pqxx>

#include "DatabaseManager.hpp"

void load_env(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "⚠️ Warning: Could not open " << path << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t delimiter_pos = line.find('=');
        if (delimiter_pos != std::string::npos) {
            std::string key = line.substr(0, delimiter_pos);
            std::string value = line.substr(delimiter_pos + 1);
            setenv(key.c_str(), value.c_str(), 1);
        }
    }
}

std::string get_db_connection_string() {

     load_env("../.env");

    const char* host = std::getenv("DB_HOST");
    const char* port = std::getenv("DB_PORT");
    const char* name = std::getenv("DB_NAME");
    const char* user = std::getenv("DB_USER");
    const char* pass = std::getenv("DB_PASS");

    if (!host || !port || !name || !user || !pass) {
        std::cerr << "Error: Missing DB configuration in environment variables!" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::string conn_str = "host=" + std::string(host) +
                           " port=" + std::string(port) +
                           " dbname=" + std::string(name) +
                           " user=" + std::string(user) +
                           " password=" + std::string(pass) +
                           " sslmode=require";
    return conn_str;

};
int main() {
    std::string conn_str = get_db_connection_string();

    DatabaseManager db_manager (conn_str);

    db_manager.create_tables();
    db_manager.insert_message("test_user", "admin", "Hello, this is a test message!");


    return 0;
}