#pragma once

#include <cstddef>
#include <string_view>

// ---------------------------------------------------------------------------
// Parsed HTTP request — method and path are string_views into the caller's
// buffer. They are only valid while that buffer is alive and unmodified.
// ---------------------------------------------------------------------------
struct HttpRequest {
    std::string_view method;
    std::string_view path;
    bool valid = false;
};

// Read raw bytes from client_fd into buf via recv() loop, detect the
// header boundary (\r\n\r\n), and extract method + path with zero
// heap allocations. Sends 400 if the request exceeds buf_size.
// Returns an invalid request on disconnect or malformed input.
HttpRequest read_request(int client_fd, char* buf, std::size_t buf_size);
