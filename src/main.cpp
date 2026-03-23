#include "server.h"

#include <iostream>
#include <cstdlib>
#include <csignal>
#include <string>

// Global server pointer for signal handling — necessary because
// signal handlers must be free functions (no captures/closures).
static Server* g_server = nullptr;

void signal_handler(int signum) {
    (void)signum;
    std::cout << "\n[CacheDB] Shutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = 6379;

    // Parse optional --port flag
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
    }

    try {
        Server server(port);
        g_server = &server;

        // Register signal handlers for graceful shutdown
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        server.start();
    } catch (const std::exception& e) {
        std::cerr << "[CacheDB] Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
