#ifndef SERVER_UTIL_HPP
#define SERVER_UTIL_HPP

#include <string>


std::string read_file(const std::string& filename);
void handle_client(int client_socket);  

#endif