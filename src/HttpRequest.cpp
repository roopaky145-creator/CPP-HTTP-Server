#include "HttpRequest.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string_view>

#include <sys/socket.h>

// ---------------------------------------------------------------------------
// Send a minimal HTTP error response and return.
// Used for DoS rejection before the full response machinery exists.
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
    int total_sent = 0;
    while (total_sent < len) {
        int sent = ::send(fd, resp + total_sent, len - total_sent, 0);
        if (sent <= 0) break;
        total_sent += sent;
    }
}

// ---------------------------------------------------------------------------
// Read and parse an HTTP request from the client socket.
//
// 1. recv() loop fills buf, tracking total bytes received.
// 2. After each read, check for the \r\n\r\n header boundary.
// 3. If the buffer fills before the boundary is found → 400 Bad Request.
// 4. Extract method and path via string_view — no heap allocations.
//
// The returned string_views point directly into buf, so the caller must
// keep buf alive and unmodified for as long as the views are used.
// ---------------------------------------------------------------------------
#include <atomic>

HttpRequest read_request(int client_fd, char* buf, std::size_t buf_size, const std::atomic<bool>& stop_flag) {
    HttpRequest req;
    int total = 0;

    // -- Read loop ----------------------------------------------------------
    while (true) {
        int n = ::recv(client_fd, buf + total,
                       static_cast<int>(buf_size) - total, 0);
        if (n < 0) {
            if ((errno == EAGAIN || errno == EWOULDBLOCK) && !stop_flag.load()) continue;
            return req; // disconnected or error or stopping
        }
        if (n == 0) return req;

        total += n;

        // Check for the end-of-headers marker
        std::string_view view(buf, total);
        if (view.find("\r\n\r\n") != std::string_view::npos) break;

        // DoS guard: reject before the buffer overflows
        if (total >= static_cast<int>(buf_size)) {
            send_error(client_fd, "400 Bad Request", "Request too large\n");
            return req;
        }
    }

    // -- Parse request line -------------------------------------------------
    std::string_view view(buf, total);

    // Method: everything before the first space
    auto method_end = view.find(' ');
    if (method_end == std::string_view::npos) return req;
    req.method = view.substr(0, method_end);

    // Path: between the first and second space
    auto path_start = method_end + 1;
    auto path_end   = view.find(' ', path_start);
    if (path_end == std::string_view::npos) return req;
    req.path = view.substr(path_start, path_end - path_start);

    req.valid = true;
    return req;
}
