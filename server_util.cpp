#include <fstream>
#include <sstream>


std::string read_file(const std::string& filename){


    std::ifstream file(filename);
    if (!file.is_open()) return "HTTP/1.1 404 Not Found\r\n\r\n";
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/html\r\n"
           "Content-Length: " + std::to_string(content.length()) + "\r\n"
           "Connection: close\r\n\r\n" + content;
}