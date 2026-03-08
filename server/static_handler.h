#pragma once
#include "http_handler.h"
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unistd.h>

// ─────────────────────────────────────────────
// StaticHandler — serves static files from a base directory
//
//   Catch-all handler: returns true for any GET that maps to
//   an existing file, or falls back to index.html for SPA routes.
//   Also handles /chat and /admin page serving.
// ─────────────────────────────────────────────
class StaticHandler : public HttpHandler {
private:
    std::string base_dir;        // e.g. "../portfolio copy/dist"
    std::string chat_html;       // e.g. "resources/static/index.html"
    std::string chat_css;        // e.g. "resources/static/style.css"
    std::string admin_html;      // e.g. "admin/index.html"
    std::string admin_css;       // e.g. "admin/styles.css"

    static bool str_ends_with(const std::string& s, const std::string& suffix) {
        if (suffix.size() > s.size()) return false;
        return s.rfind(suffix) == (s.size() - suffix.size());
    }

    static std::string get_mime_type(const std::string& path) {
        if (str_ends_with(path, ".html")) return "text/html";
        if (str_ends_with(path, ".js"))   return "application/javascript";
        if (str_ends_with(path, ".css"))  return "text/css";
        if (str_ends_with(path, ".png"))  return "image/png";
        if (str_ends_with(path, ".json")) return "application/json";
        if (str_ends_with(path, ".svg"))  return "image/svg+xml";
        if (str_ends_with(path, ".jpeg")) return "image/jpeg";
        if (str_ends_with(path, ".jpg"))  return "image/jpeg";
        if (str_ends_with(path, ".ico"))  return "image/x-icon";
        return "text/plain";
    }

    bool is_path_safe(const std::string& file_path) {
        auto abs_p    = std::filesystem::weakly_canonical(std::filesystem::path(file_path));
        auto abs_base = std::filesystem::weakly_canonical(std::filesystem::path(base_dir));
        auto it      = abs_p.begin();
        auto it_base = abs_base.begin();
        for (; it_base != abs_base.end(); ++it, ++it_base) {
            if (it == abs_p.end() || *it != *it_base)
                return false;
        }
        return true;
    }

    void serve_file(int socket, const std::string& filepath, const std::string& mime_type) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            send_404(socket);
            return;
        }
        std::stringstream buf;
        buf << file.rdbuf();
        std::string content = buf.str();

        // Cache assets (JS, CSS, images) for 1 hour; HTML always revalidates
        std::string cache_control = (mime_type == "text/html")
            ? "no-cache"
            : "public, max-age=3600";

        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: " + mime_type + "\r\n"
            "Content-Length: " + std::to_string(content.length()) + "\r\n"
            "Cache-Control: " + cache_control + "\r\n"
            "Connection: close\r\n\r\n" + content;
        write(socket, response.c_str(), response.length());
    }

    void send_404(int socket) {
        std::string r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write(socket, r.c_str(), r.length());
    }

    void send_403(int socket) {
        std::string r = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write(socket, r.c_str(), r.length());
    }

public:
    StaticHandler(const std::string& base_dir,
                  const std::string& chat_html = "resources/static/index.html",
                  const std::string& chat_css  = "resources/static/style.css",
                  const std::string& admin_html = "admin/index.html",
                  const std::string& admin_css  = "admin/styles.css")
        : base_dir(base_dir), chat_html(chat_html), chat_css(chat_css),
          admin_html(admin_html), admin_css(admin_css) {}

    // This is a catch-all — always matches
    std::string path_prefix() const override { return "/"; }

    bool handle(int socket,
            const std::string& method, const std::string& path,
            const std::string& query,
            const std::unordered_map<std::string, std::string>& /*headers*/) override {

        if (method != "GET") return false;

        // ── Special pages ──
        if (path == "/chat") {
            serve_file(socket, chat_html, "text/html");
            return true;
        }
        if (path == "/style.css") {
            serve_file(socket, chat_css, "text/css");
            return true;
        }
        if (path == "/admin") {
            serve_file(socket, admin_html, "text/html");
            return true;
        }
        if (path == "/admin/styles.css") {
            serve_file(socket, admin_css, "text/css");
            return true;
        }

        // ── Portfolio static files ──
        if (path == "/") {
            serve_file(socket, base_dir + "/index.html", "text/html");
            return true;
        }

        std::string local_path = base_dir + path;
        if (!is_path_safe(local_path)) {
            send_403(socket);
            return true;
        }
        if (std::filesystem::exists(local_path) && !std::filesystem::is_directory(local_path)) {
            serve_file(socket, local_path, get_mime_type(local_path));
            return true;
        }

        // SPA fallback — serve index.html for unknown client-side routes
        serve_file(socket, base_dir + "/index.html", "text/html");
        return true;
    }
};
