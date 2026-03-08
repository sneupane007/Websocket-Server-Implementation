#pragma once
#include <string>
#include <unordered_map>

// ─────────────────────────────────────────────
// HttpHandler — interface for HTTP request handlers
//
//   Any feature that serves HTTP requests (REST API, static
//   files, etc.) implements this.  The HttpRouter matches
//   incoming GET/POST requests by path_prefix() and dispatches.
// ─────────────────────────────────────────────
class HttpHandler {
public:
    virtual ~HttpHandler() = default;

    // The URL path prefix this handler owns, e.g. "/api/", "/admin"
    virtual std::string path_prefix() const = 0;

    // Handle an HTTP request.
    //   method:  "GET", "POST", etc.
    //   path:    the full path (e.g. "/api/visitors")
    //   query:   the query string (after '?'), may be empty
    //   headers: parsed HTTP headers
    // Returns true if this handler consumed the request, false to pass to next handler.
    virtual bool handle(int socket,
            const std::string& method, const std::string& path,
            const std::string& query,
            const std::unordered_map<std::string, std::string>& headers) = 0;
};
