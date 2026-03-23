#pragma once

#include "store.h"
#include "command_parser.h"
#include "protocol.h"
#include <string>

// Cross-platform socket type
#ifdef _WIN32
    #include <winsock2.h>
    using socket_t = SOCKET;
    #define INVALID_SOCK INVALID_SOCKET
#else
    using socket_t = int;
    #define INVALID_SOCK (-1)
#endif

/**
 * Handles a single client connection.
 *
 * Reads commands line-by-line, parses them, executes against the
 * shared KVStore, and sends formatted responses back.
 */
class ClientHandler {
public:
    ClientHandler(socket_t client_fd, KVStore& store);

    // Main loop — blocks until client disconnects or error
    void handle();

private:
    socket_t    client_fd_;
    KVStore&    store_;

    // Send a string response to the client
    bool send_response(const std::string& response);

    // Dispatch a parsed command to the store and return the response
    std::string execute(const ParsedCommand& cmd);
};
