#ifndef REST_CONTROLLER_H
#define REST_CONTROLLER_H

#include <string>
#include "../database/DatabaseManager.hpp"
#include "ClientManager.h"

class RestController {
private:
    DatabaseManager& db;
    ClientManager&   clients;

    // Reads a file from disk and wraps it in an HTTP 200 response
    std::string build_file_response(const std::string& filepath, const std::string& mime_type);

public:
    RestController(DatabaseManager& db, ClientManager& clients);

    // Static file serving
    void serve_file(int socket, const std::string& filepath, const std::string& mime_type);

    // REST API endpoints
    void handle_get_visitors(int socket);
    void handle_get_history(int socket, const std::string& path);

    // Fallback
    void send_403(int socket);
    void send_404(int socket);
};

#endif
