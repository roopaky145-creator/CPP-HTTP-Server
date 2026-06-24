#include <atomic>
#include <csignal>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "FdHandle.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "ThreadPool.h"

// ---------------------------------------------------------------------------
// Global shutdown flag — set by SIGINT handler, read by accept loop
// ---------------------------------------------------------------------------
std::atomic<bool> g_stop{false};

// ---------------------------------------------------------------------------
// Server configuration parsed from CLI arguments
// ---------------------------------------------------------------------------
struct Config {
    int         port    = 8080;
    std::string root    = "./public";
    int         threads = static_cast<int>(std::thread::hardware_concurrency());
};

static Config parse_args(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            cfg.port = std::atoi(argv[++i]);
        } else if (arg == "--root" && i + 1 < argc) {
            cfg.root = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            cfg.threads = std::atoi(argv[++i]);
        }
    }
    // Fallback if hardware_concurrency() returns 0 or user passes garbage
    if (cfg.threads <= 0) cfg.threads = 4;
    return cfg;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Prevent SIGPIPE from killing the process on broken client connections
    signal(SIGPIPE, SIG_IGN);

    // Wire Ctrl+C to the global stop flag for graceful shutdown
    signal(SIGINT, [](int) { g_stop.store(true); });

    Config cfg = parse_args(argc, argv);

    // Resolve the document root to an absolute path at startup so that
    // all subsequent path operations work against a canonical base.
    char resolved_root[PATH_MAX];
    if (!realpath(cfg.root.c_str(), resolved_root)) {
        std::cerr << "error: cannot resolve root directory '" << cfg.root
                  << "': " << std::strerror(errno) << "\n";
        return 1;
    }
    std::string root = resolved_root;

    std::cout << "server starting on port " << cfg.port
              << " | root: " << root
              << " | threads: " << cfg.threads << "\n";

    // -----------------------------------------------------------------------
    // Socket initialisation: create, configure, bind, listen
    // -----------------------------------------------------------------------
    int raw_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (raw_fd < 0) {
        std::cerr << "error: socket() failed: " << std::strerror(errno) << "\n";
        return 1;
    }
    FdHandle server_fd(raw_fd);

    // SO_REUSEADDR lets us restart immediately after a crash without
    // waiting for the kernel's TIME_WAIT to expire.
    int opt = 1;
    if (::setsockopt(server_fd.get(), SOL_SOCKET, SO_REUSEADDR,
                     &opt, sizeof(opt)) < 0) {
        std::cerr << "error: setsockopt(SO_REUSEADDR) failed: "
                  << std::strerror(errno) << "\n";
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(cfg.port));

    if (::bind(server_fd.get(),
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "error: bind() failed on port " << cfg.port
                  << ": " << std::strerror(errno) << "\n";
        return 1;
    }

    if (::listen(server_fd.get(), SOMAXCONN) < 0) {
        std::cerr << "error: listen() failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    std::cout << "listening on 0.0.0.0:" << cfg.port << "\n";

    // -----------------------------------------------------------------------
    // Thread pool — workers wait on a condition variable for dispatched fds
    // -----------------------------------------------------------------------
    ThreadPool pool(cfg.threads, g_stop, [&root](int client_fd) {
        char buf[8192];
        HttpRequest req = read_request(client_fd, buf, sizeof(buf));
        if (!req.valid) return;  // disconnect, malformed, or 400 already sent

        handle_request(client_fd, req, root);
    });

    // -----------------------------------------------------------------------
    // Accept loop — poll() with 1s timeout so we can check g_stop regularly
    // -----------------------------------------------------------------------
    pollfd pfd{};
    pfd.fd     = server_fd.get();
    pfd.events = POLLIN;

    while (!g_stop.load()) {
        int ready = ::poll(&pfd, 1, 1000);
        if (ready < 0) {
            if (errno == EINTR) continue;   // interrupted by signal, retry
            std::cerr << "error: poll() failed: " << std::strerror(errno) << "\n";
            break;
        }
        if (ready == 0) continue;           // timeout, re-check g_stop

        int client_raw = ::accept(server_fd.get(), nullptr, nullptr);
        if (client_raw < 0) {
            if (errno == EINTR) continue;   // signal during accept, retry
            std::cerr << "warning: accept() failed: "
                      << std::strerror(errno) << "\n";
            continue;
        }
        FdHandle client(client_raw);

        // Transfer fd ownership across the thread boundary.
        // release() hands the raw int to the pool; the worker re-wraps it.
        pool.submit(client.release());
    }

    // -----------------------------------------------------------------------
    // Graceful shutdown — wake all workers and wait for them to finish
    // -----------------------------------------------------------------------
    std::cout << "\nshutting down...\n";
    g_stop.store(true);             // ensure workers see stop even if loop
                                    // broke on a poll() error, not SIGINT
    pool.shutdown();
    std::cout << "all workers joined, exiting.\n";

    return 0;
}
