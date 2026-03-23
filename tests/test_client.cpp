/**
 * test_client.cpp — Functional test suite for CacheDB
 *
 * Connects to a running CacheDB server and validates all supported
 * commands. Run the server first, then run this test client.
 *
 * Usage: ./test_client [host] [port]
 */

#include <iostream>
#include <string>
#include <cstring>
#include <cassert>
#include <thread>
#include <chrono>

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

// ── Helpers ───────────────────────────────────────────────────────

static socket_t connect_to(const std::string& host, int port) {
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) {
        throw std::runtime_error("Failed to create socket");
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
                sizeof(addr)) < 0) {
        close_socket(fd);
        throw std::runtime_error("Failed to connect to " + host + ":" +
                                 std::to_string(port));
    }
    return fd;
}

static std::string send_command(socket_t fd, const std::string& cmd) {
    std::string full = cmd + "\n";
    send(fd, full.c_str(), static_cast<int>(full.size()), 0);

    // Small delay to ensure server processes the command
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    char buffer[4096];
    int n = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) return "";
    buffer[n] = '\0';
    return std::string(buffer);
}

// ── Test Macros ───────────────────────────────────────────────────

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name, condition)                                            \
    do {                                                                  \
        if (condition) {                                                   \
            std::cout << "  [PASS] " << name << std::endl;                 \
            tests_passed++;                                                \
        } else {                                                           \
            std::cout << "  [FAIL] " << name << std::endl;                 \
            tests_failed++;                                                \
        }                                                                  \
    } while (0)

#define TEST_EQ(name, actual, expected)                                   \
    do {                                                                   \
        std::string a = (actual);                                          \
        std::string e = (expected);                                        \
        if (a == e) {                                                      \
            std::cout << "  [PASS] " << name << std::endl;                 \
            tests_passed++;                                                \
        } else {                                                           \
            std::cout << "  [FAIL] " << name                               \
                      << "  (expected: \"" << e                            \
                      << "\", got: \"" << a << "\")" << std::endl;         \
            tests_failed++;                                                \
        }                                                                  \
    } while (0)

// ── Tests ─────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 6379;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::atoi(argv[2]);

    std::cout << "Connecting to CacheDB at " << host << ":" << port
              << "...\n" << std::endl;

    socket_t fd;
    try {
        fd = connect_to(host, port);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what()
                  << "\nMake sure the CacheDB server is running." << std::endl;
        return 1;
    }

    // ── PING ──
    std::cout << "=== PING ===" << std::endl;
    TEST_EQ("PING returns PONG",
            send_command(fd, "PING"), "+PONG\n");

    // ── SET / GET ──
    std::cout << "\n=== SET / GET ===" << std::endl;
    TEST_EQ("SET key returns OK",
            send_command(fd, "SET mykey myvalue"), "+OK\n");
    TEST_EQ("GET existing key returns value",
            send_command(fd, "GET mykey"), "$myvalue\n");
    TEST_EQ("GET missing key returns NIL",
            send_command(fd, "GET nonexistent"), "$NIL\n");

    // ── EXISTS ──
    std::cout << "\n=== EXISTS ===" << std::endl;
    TEST_EQ("EXISTS on existing key returns 1",
            send_command(fd, "EXISTS mykey"), ":1\n");
    TEST_EQ("EXISTS on missing key returns 0",
            send_command(fd, "EXISTS nokey"), ":0\n");

    // ── DEL ──
    std::cout << "\n=== DEL ===" << std::endl;
    send_command(fd, "SET delme goodbye");
    TEST_EQ("DEL existing key returns 1",
            send_command(fd, "DEL delme"), ":1\n");
    TEST_EQ("DEL missing key returns 0",
            send_command(fd, "DEL delme"), ":0\n");
    TEST_EQ("GET after DEL returns NIL",
            send_command(fd, "GET delme"), "$NIL\n");

    // ── KEYS ──
    std::cout << "\n=== KEYS ===" << std::endl;
    send_command(fd, "SET alpha 1");
    send_command(fd, "SET beta 2");
    std::string keys_resp = send_command(fd, "KEYS");
    // Response should contain both keys (order not guaranteed)
    TEST("KEYS response contains alpha",
         keys_resp.find("$alpha\n") != std::string::npos);
    TEST("KEYS response contains beta",
         keys_resp.find("$beta\n") != std::string::npos);

    // ── EXPIRE / TTL ──
    std::cout << "\n=== EXPIRE / TTL ===" << std::endl;
    send_command(fd, "SET ttlkey data");
    TEST_EQ("TTL on key without expiry returns -1",
            send_command(fd, "TTL ttlkey"), ":-1\n");
    TEST_EQ("EXPIRE sets TTL successfully",
            send_command(fd, "EXPIRE ttlkey 10"), ":1\n");

    std::string ttl_resp = send_command(fd, "TTL ttlkey");
    // TTL should be between 1 and 10
    TEST("TTL returns positive value", ttl_resp.find(":") == 0);
    TEST_EQ("EXPIRE on missing key returns 0",
            send_command(fd, "EXPIRE nokey 10"), ":0\n");
    TEST_EQ("TTL on missing key returns -2",
            send_command(fd, "TTL nokey"), ":-2\n");

    // ── Expiration ──
    std::cout << "\n=== Key Expiration ===" << std::endl;
    send_command(fd, "SET shortlived value");
    send_command(fd, "EXPIRE shortlived 1");
    TEST_EQ("Key exists before expiry",
            send_command(fd, "EXISTS shortlived"), ":1\n");
    std::cout << "  Waiting 2 seconds for key to expire..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    TEST_EQ("Key gone after expiry",
            send_command(fd, "GET shortlived"), "$NIL\n");

    // ── Error handling ──
    std::cout << "\n=== Error Handling ===" << std::endl;
    std::string err = send_command(fd, "SET");
    TEST("SET without args returns error",
         err.find("-ERR") != std::string::npos);
    err = send_command(fd, "BOGUS");
    TEST("Unknown command returns error",
         err.find("-ERR") != std::string::npos);

    // ── SAVE ──
    std::cout << "\n=== SAVE ===" << std::endl;
    TEST_EQ("SAVE returns OK",
            send_command(fd, "SAVE"), "+OK\n");

    // ── Cleanup ──
    send_command(fd, "DEL mykey");
    send_command(fd, "DEL alpha");
    send_command(fd, "DEL beta");
    send_command(fd, "DEL ttlkey");
    send_command(fd, "QUIT");
    close_socket(fd);

    // ── Summary ──
    std::cout << "\n════════════════════════════════════════" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    std::cout << "════════════════════════════════════════" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
