/**
 * benchmark.cpp — Throughput benchmark for CacheDB
 *
 * Spawns N concurrent client threads, each performing M SET + GET
 * operations, and measures total throughput in operations/second.
 *
 * Usage: ./benchmark [host] [port] [threads] [ops_per_thread]
 */

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
    #endif
    using socket_t = SOCKET;
    #define INVALID_SOCK INVALID_SOCKET
    inline void close_socket(socket_t s) { closesocket(s); }

    struct WinsockInit {
        WinsockInit() {
            WSADATA wsa;
            WSAStartup(MAKEWORD(2, 2), &wsa);
        }
        ~WinsockInit() { WSACleanup(); }
    };
    static WinsockInit winsock_init;
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    using socket_t = int;
    #define INVALID_SOCK (-1)
    inline void close_socket(socket_t s) { close(s); }
#endif

static std::atomic<int> total_ops{0};
static std::atomic<int> errors{0};

static socket_t connect_to(const std::string& host, int port) {
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) return INVALID_SOCK;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
                sizeof(addr)) < 0) {
        close_socket(fd);
        return INVALID_SOCK;
    }
    return fd;
}

static void send_and_recv(socket_t fd, const std::string& cmd) {
    std::string full = cmd + "\n";
    send(fd, full.c_str(), static_cast<int>(full.size()), 0);
    char buf[1024];
    recv(fd, buf, sizeof(buf) - 1, 0);
}

/**
 * Worker thread: performs ops_count SET + GET pairs against the server.
 * Each thread uses its own connection (no connection pooling).
 */
static void worker(const std::string& host, int port,
                   int thread_id, int ops_count) {
    socket_t fd = connect_to(host, port);
    if (fd == INVALID_SOCK) {
        std::cerr << "Thread " << thread_id
                  << ": failed to connect" << std::endl;
        errors.fetch_add(ops_count * 2);
        return;
    }

    for (int i = 0; i < ops_count; ++i) {
        std::string key = "bench:" + std::to_string(thread_id)
                        + ":" + std::to_string(i);
        std::string val = "value_" + std::to_string(i);

        // SET
        send_and_recv(fd, "SET " + key + " " + val);
        total_ops.fetch_add(1);

        // GET
        send_and_recv(fd, "GET " + key);
        total_ops.fetch_add(1);
    }

    // Cleanup: delete all keys we created
    for (int i = 0; i < ops_count; ++i) {
        std::string key = "bench:" + std::to_string(thread_id)
                        + ":" + std::to_string(i);
        send_and_recv(fd, "DEL " + key);
    }

    send_and_recv(fd, "QUIT");
    close_socket(fd);
}

int main(int argc, char* argv[]) {
    std::string host     = "127.0.0.1";
    int port             = 6379;
    int num_threads      = 4;
    int ops_per_thread   = 1000;

    if (argc >= 2) host           = argv[1];
    if (argc >= 3) port           = std::atoi(argv[2]);
    if (argc >= 4) num_threads    = std::atoi(argv[3]);
    if (argc >= 5) ops_per_thread = std::atoi(argv[4]);

    int total_planned = num_threads * ops_per_thread * 2;  // SET + GET

    std::cout << "════════════════════════════════════════════" << std::endl;
    std::cout << "  CacheDB Benchmark" << std::endl;
    std::cout << "════════════════════════════════════════════" << std::endl;
    std::cout << "  Server:          " << host << ":" << port << std::endl;
    std::cout << "  Threads:         " << num_threads << std::endl;
    std::cout << "  Ops/thread:      " << ops_per_thread
              << " SET + " << ops_per_thread << " GET" << std::endl;
    std::cout << "  Total ops:       " << total_planned << std::endl;
    std::cout << "────────────────────────────────────────────" << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    // Launch worker threads
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, host, port, i, ops_per_thread);
    }

    // Wait for all threads to finish
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(
        end - start
    ).count();
    double elapsed_sec = elapsed_ms / 1000.0;

    int completed = total_ops.load();
    double ops_per_sec = completed / elapsed_sec;

    std::cout << "\n  Results:" << std::endl;
    std::cout << "  ────────────────────────────────────────" << std::endl;
    std::cout << "  Completed ops:   " << completed << std::endl;
    std::cout << "  Elapsed time:    " << elapsed_ms << " ms" << std::endl;
    std::cout << "  Throughput:      " << static_cast<int>(ops_per_sec)
              << " ops/sec" << std::endl;
    if (errors.load() > 0) {
        std::cout << "  Errors:          " << errors.load() << std::endl;
    }
    std::cout << "════════════════════════════════════════════" << std::endl;

    return 0;
}
