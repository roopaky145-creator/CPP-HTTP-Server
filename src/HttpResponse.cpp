#include "HttpResponse.h"
#include "FdHandle.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

// ---------------------------------------------------------------------------
// Helper: Send all bytes, handling partial writes.
// ---------------------------------------------------------------------------
static void send_all(int fd, const char* buf, int len) {
    int total_sent = 0;
    while (total_sent < len) {
        int sent = ::send(fd, buf + total_sent, len - total_sent, 0);
        if (sent <= 0) {
            break; // connection closed or error
        }
        total_sent += sent;
    }
}

// ---------------------------------------------------------------------------
// Helper: Send a minimal HTTP error response.
// ---------------------------------------------------------------------------
static void send_error(int fd, const char* status, const char* body) {
    char resp[256];
    int len = std::snprintf(resp, sizeof(resp),
        "HTTP/1.1 %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        status, std::strlen(body), body);
    send_all(fd, resp, len);
}

// ---------------------------------------------------------------------------
// Helper: Get MIME type based on file extension.
// ---------------------------------------------------------------------------
static const char* get_mime_type(const std::string& path) {
    static const std::unordered_map<std::string, const char*> mime_types = {
        {".html", "text/html"},
        {".css",  "text/css"},
        {".js",   "application/javascript"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".ico",  "image/x-icon"},
        {".json", "application/json"},
        {".txt",  "text/plain"}
    };

    auto dot_pos = path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        auto ext = path.substr(dot_pos);
        auto it = mime_types.find(ext);
        if (it != mime_types.end()) {
            return it->second;
        }
    }
    return "application/octet-stream";
}

// ---------------------------------------------------------------------------
// Main request handler: routing, traversal protection, MIME, zero-copy send
// ---------------------------------------------------------------------------
void handle_request(int client_fd, const HttpRequest& req, const std::string& root) {
    // Only support GET requests
    if (req.method != "GET") {
        send_error(client_fd, "405 Method Not Allowed", "Only GET is supported\n");
        return;
    }

    // Strip query string if present (e.g., /index.html?v=1 -> /index.html)
    std::string req_path(req.path);
    auto q_pos = req_path.find('?');
    if (q_pos != std::string::npos) {
        req_path = req_path.substr(0, q_pos);
    }

    // Path rewrite: default to /index.html
    if (req_path == "/") {
        req_path = "/index.html";
    }

    // Construct full path
    std::string full_path = root + req_path;

    // Directory traversal protection using realpath()
    char resolved[PATH_MAX];
    if (!realpath(full_path.c_str(), resolved)) {
        send_error(client_fd, "404 Not Found", "File not found\n");
        return;
    }

    std::string resolved_str(resolved);
    std::string root_prefix = root + "/";
    // Ensure the resolved path strictly starts with the document root
    if (resolved_str.substr(0, root_prefix.size()) != root_prefix && resolved_str != root) {
        send_error(client_fd, "403 Forbidden", "Access denied\n");
        return;
    }

    // Get file size using stat()
    struct stat file_stat;
    if (::stat(resolved, &file_stat) < 0 || S_ISDIR(file_stat.st_mode)) {
        send_error(client_fd, "404 Not Found", "File not found\n");
        return;
    }

    // Open file
    int file_raw = ::open(resolved, O_RDONLY);
    if (file_raw < 0) {
        send_error(client_fd, "403 Forbidden", "Access denied\n");
        return;
    }
    FdHandle file_fd(file_raw);

    const char* mime = get_mime_type(resolved_str);

    // Send HTTP headers
    char headers[512];
    int hdr_len = std::snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime, file_stat.st_size);
    send_all(client_fd, headers, hdr_len);

    // Zero-copy file transfer using sendfile() in a loop to handle partial writes
    off_t offset = 0;
    size_t remaining = file_stat.st_size;
    while (remaining > 0) {
        ssize_t sent = ::sendfile(client_fd, file_fd.get(), &offset, remaining);
        if (sent <= 0) {
            break; // connection closed or error
        }
        remaining -= sent;
    }
}
