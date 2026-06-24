# CPP HTTP Server 🚀

A high-performance, multithreaded HTTP server written from scratch in C++17. Designed to minimize overhead and maximize throughput using modern C++ abstractions and zero-copy kernel I/O.

## Features

- **Custom Thread Pool:** A concurrent worker pool utilizing `std::mutex` and `std::condition_variable` for lock-based task dispatching.
- **Zero-Allocation Parsing:** Request parsing utilizes `std::string_view` mapped directly to stack-allocated fixed-size buffers (8KB), avoiding all heap allocations during the read phase.
- **Robust Connection Handling:** Graceful shutdown mechanics, raw file descriptor ownership transfer using RAII wrappers (`FdHandle`), and explicit handling of `EINTR` signals.
- **Zero-Copy File Serving:** Uses the Linux `sendfile()` system call to transfer file contents directly from the OS page cache to the network socket, bypassing user-space memory entirely.
- **Security:** Built-in Denial-of-Service (DoS) mitigation against oversized headers and strict directory traversal (`../`) protections using `realpath()`.

## Architecture

The server operates using an Event-Loop + Worker Pool pattern:
1. The **Main Thread** blocks on `poll()` with a short timeout to handle `SIGINT` checks.
2. Incoming connections are accepted and their raw file descriptors are handed off to the concurrent `ThreadPool`.
3. A **Worker Thread** wakes up, assumes RAII ownership of the socket, and reads the HTTP request into a fixed-size stack buffer.
4. The router verifies path safety, determines the correct MIME type (`std::unordered_map`), and replies with the appropriate HTTP headers.
5. The requested file is streamed to the client using `sendfile()`.

## Build Instructions

This project requires a Linux environment (or WSL2) due to its use of Linux-specific APIs (`poll`, `sendfile`).

### Dependencies
- `g++` (C++17 support)
- `cmake`
- `make`

### Building
```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

### Running
```bash
./server --port 8080 --root ../public
```

## Performance Benchmarks

Benchmarks were performed using `wrk` with 4 threads and 100 concurrent connections over a 10-second duration, serving a static HTML file via `sendfile()`.

**Environment:** WSL2 (Ubuntu 24.04) on Windows

```
Running 10s test @ http://localhost:8080/index.html
  4 threads and 100 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    23.62ms    1.88ms  45.27ms   82.93%
    Req/Sec     1.06k    62.58     1.22k    77.75%
  42163 requests in 10.03s, 62.97MB read
Requests/sec:   4203.94
Transfer/sec:      6.28MB
```

## Security & Reliability Note

This project focuses on the core mechanics of a bare-metal HTTP server. It successfully implements foundational protections against basic DoS and directory traversal attacks. 
