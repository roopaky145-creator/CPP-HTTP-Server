#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Fixed-size thread pool with a shared work queue.
// The accept loop pushes raw file descriptors via submit(); worker threads
// pop them, re-wrap in FdHandle for RAII safety, and invoke the handler.
// ---------------------------------------------------------------------------
class ThreadPool {
public:
    // handler is called for each accepted client fd inside a worker thread
    ThreadPool(int num_threads, std::atomic<bool>& stop_flag,
               std::function<void(int)> handler);
    ~ThreadPool();

    // Transfer ownership of a client fd to the work queue.
    // The caller must have released the fd from any RAII wrapper first.
    void submit(int fd);

    // Wake all suspended workers and block until they exit.
    void shutdown();

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    void worker_loop();

    std::vector<std::thread>   workers_;
    std::deque<int>            queue_;
    std::mutex                 mtx_;
    std::condition_variable    cv_;
    std::atomic<bool>&         stop_;
    std::function<void(int)>   handler_;
};
