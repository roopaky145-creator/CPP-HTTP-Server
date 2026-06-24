#include "ThreadPool.h"
#include "FdHandle.h"

// ---------------------------------------------------------------------------
// Constructor — spins up the worker threads immediately
// ---------------------------------------------------------------------------
ThreadPool::ThreadPool(int num_threads, std::atomic<bool>& stop_flag,
                       std::function<void(int)> handler)
    : stop_(stop_flag)
    , handler_(std::move(handler))
{
    workers_.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

// ---------------------------------------------------------------------------
// Dispatcher — called from the accept loop to enqueue a client fd.
// Locks the mutex, pushes the raw fd, and wakes one sleeping worker.
// ---------------------------------------------------------------------------
void ThreadPool::submit(int fd) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.push_back(fd);
    }
    cv_.notify_one();
}

// ---------------------------------------------------------------------------
// Shutdown — wake all workers and block until every thread has joined.
// Safe to call multiple times (joinable check prevents double-join).
// ---------------------------------------------------------------------------
void ThreadPool::shutdown() {
    stop_.store(true);
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

// ---------------------------------------------------------------------------
// Worker loop — each thread runs this until g_stop is set.
//
// 1. Lock the mutex and wait until the queue is non-empty or stop is set.
// 2. If the queue is empty after waking (shutdown scenario), break out
//    to avoid popping from an empty deque.
// 3. Pop the raw fd, release the lock, then immediately re-wrap it in
//    FdHandle so the descriptor is always closed — even if the handler
//    throws or returns early.
// ---------------------------------------------------------------------------
void ThreadPool::worker_loop() {
    while (true) {
        int fd;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] {
                return !queue_.empty() || stop_.load();
            });

            // Shutdown with empty queue — exit cleanly
            if (queue_.empty()) break;

            fd = queue_.front();
            queue_.pop_front();
        }

        // RAII re-wrap: guarantees close() even if handler throws
        FdHandle client(fd);
        handler_(client.get());
    }
}
