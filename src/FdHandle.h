#pragma once

#include <memory>
#include <unistd.h>

// ---------------------------------------------------------------------------
// RAII file descriptor wrapper via unique_ptr's custom pointer trick.
// The nested 'pointer' type satisfies NullablePointer without any heap
// allocation — unique_ptr stores the wrapped int directly.
// ---------------------------------------------------------------------------
struct FdDeleter {
    struct pointer {
        int fd_;
        pointer() noexcept : fd_(-1) {}
        pointer(std::nullptr_t) noexcept : fd_(-1) {}
        pointer(int fd) noexcept : fd_(fd) {}
        explicit operator bool() const noexcept { return fd_ != -1; }
        operator int() const noexcept { return fd_; }
        friend bool operator==(pointer a, pointer b) noexcept { return a.fd_ == b.fd_; }
        friend bool operator!=(pointer a, pointer b) noexcept { return a.fd_ != b.fd_; }
    };
    void operator()(pointer p) const noexcept {
        if (p) ::close(p);
    }
};
using FdHandle = std::unique_ptr<int, FdDeleter>;
