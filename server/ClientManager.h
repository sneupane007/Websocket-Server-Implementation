#pragma once
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>

enum class ClientRole {
    VISITOR,
    ADMIN
};

struct ClientInfo {
    int socket;
    ClientRole role;
    std::string id; 
};

class ClientManager {
private:
    std::map<int, ClientInfo> clients;
    std::mutex manager_mutex;

public:
    void add_client(int socket, ClientRole role, std::string id) {
        std::lock_guard<std::mutex> lock(manager_mutex);
        clients[socket] = {socket, role, id};
        std::cout << "Client added: " << id << " (Role: " << (role == ClientRole::ADMIN ? "Admin" : "Visitor") << ")" << std::endl;
    }

    void remove_client(int socket) {
        std::lock_guard<std::mutex> lock(manager_mutex);
        if (clients.count(socket)) {
            std::cout << "Client removed: " << clients[socket].id << std::endl;
            clients.erase(socket);
        }
    }

    std::vector<int> get_admin_sockets() {
        std::lock_guard<std::mutex> lock(manager_mutex);
        std::vector<int> admins;
        for (auto const& [sock, info] : clients) {
            if (info.role == ClientRole::ADMIN) {
                admins.push_back(sock);
            }
        }
        return admins;
    }

    int get_visitor_socket(const std::string& visitor_id) {
        std::lock_guard<std::mutex> lock(manager_mutex);
        for (auto const& [sock, info] : clients) {
            if (info.role == ClientRole::VISITOR && info.id == visitor_id) {
                return sock;
            }
        }
        return -1;
    }
    
    std::vector<std::string> get_active_visitors() {
        std::lock_guard<std::mutex> lock(manager_mutex);
        std::vector<std::string> visitors;
        for (auto const& [sock, info] : clients) {
            if (info.role == ClientRole::VISITOR) {
                visitors.push_back(info.id);
            }
        }
        return visitors;
    }
};
