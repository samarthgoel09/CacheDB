#include "server.h"
#include "client_handler.h"

#include <iostream>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
    #endif

    // RAII wrapper for Winsock initialization
    struct WinsockInit {
        WinsockInit() {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
                throw std::runtime_error("WSAStartup failed");
            }
        }
        ~WinsockInit() { WSACleanup(); }
    };
    static WinsockInit winsock_init;

    inline void close_socket(socket_t s) { closesocket(s); }
    #define INVALID_SOCK INVALID_SOCKET
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <signal.h>

    inline void close_socket(socket_t s) { close(s); }
    #define INVALID_SOCK (-1)
#endif

Server::Server(uint16_t port)
    : port_(port), listen_fd_(INVALID_SOCK)
{}

Server::~Server() {
    stop();
    cleanup();
}

void Server::start() {
    #ifndef _WIN32
    // Ignore SIGPIPE so writes to closed sockets don't kill us
    signal(SIGPIPE, SIG_IGN);
    #endif

    init_socket();
    running_.store(true);
    std::cout << "[CacheDB] Server listening on port " << port_ << std::endl;
    accept_loop();
}

void Server::stop() {
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false)) {
        // Close the listening socket to unblock accept()
        if (listen_fd_ != INVALID_SOCK) {
            close_socket(listen_fd_);
            listen_fd_ = INVALID_SOCK;
        }
        store_.shutdown();
    }
}

void Server::init_socket() {
    // Create TCP socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == INVALID_SOCK) {
        throw std::runtime_error("Failed to create socket");
    }

    // Allow port reuse — avoids "Address already in use" on restart
    int opt = 1;
    #ifdef _WIN32
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
    #else
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));
    #endif

    // Bind to all interfaces on the configured port
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
             sizeof(addr)) < 0) {
        close_socket(listen_fd_);
        throw std::runtime_error("Failed to bind to port " +
                                 std::to_string(port_));
    }

    // Listen with a backlog of 128 pending connections
    if (listen(listen_fd_, 128) < 0) {
        close_socket(listen_fd_);
        throw std::runtime_error("Failed to listen on socket");
    }
}

void Server::accept_loop() {
    while (running_.load()) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        socket_t client_fd = accept(
            listen_fd_,
            reinterpret_cast<struct sockaddr*>(&client_addr),
            &client_len
        );

        if (client_fd == INVALID_SOCK) {
            if (!running_.load()) {
                break;  // Server is shutting down — expected
            }
            std::cerr << "[CacheDB] Accept failed, continuing..." << std::endl;
            continue;
        }

        // Spawn a dedicated thread for this client.
        // We detach OR join later — here we store the thread for cleanup.
        client_threads_.emplace_back([this, client_fd]() {
            ClientHandler handler(client_fd, store_);
            handler.handle();
        });
    }
}

void Server::cleanup() {
    // Wait for all client handler threads to finish
    for (auto& t : client_threads_) {
        if (t.joinable()) {
            t.detach();  // Don't block shutdown on slow clients
        }
    }
    client_threads_.clear();
}
