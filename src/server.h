#pragma once

#include "store.h"
#include <atomic>
#include <vector>
#include <thread>
#include <cstdint>

#ifdef _WIN32
    #include <winsock2.h>
    using socket_t = SOCKET;
#else
    using socket_t = int;
#endif

/**
 * TCP server that accepts client connections and spawns a
 * handler thread for each one.
 *
 * Uses RAII to manage the listening socket — guaranteed cleanup
 * even on exceptions. Supports graceful shutdown via stop().
 */
class Server {
public:
    explicit Server(uint16_t port = 6379);
    ~Server();

    // Disallow copy
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Start accepting connections (blocks until stop() is called)
    void start();

    // Signal the server to stop accepting new connections
    void stop();

private:
    uint16_t             port_;
    socket_t             listen_fd_;
    std::atomic<bool>    running_{false};
    KVStore              store_;
    std::vector<std::thread> client_threads_;

    void init_socket();
    void accept_loop();
    void cleanup();
};
