#include "client_handler.h"

#include <iostream>
#include <sstream>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    // recv/send already work with SOCKET on Windows
    inline void close_socket(socket_t s) { closesocket(s); }
#else
    #include <unistd.h>
    #include <sys/socket.h>
    inline void close_socket(socket_t s) { close(s); }
#endif

ClientHandler::ClientHandler(socket_t client_fd, KVStore& store)
    : client_fd_(client_fd), store_(store)
{}

void ClientHandler::handle() {
    // Buffer for incoming data — we read in chunks and split on newlines
    // because TCP is a stream protocol with no message boundaries.
    constexpr size_t BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];
    std::string leftover;  // Partial data from previous recv()

    while (true) {
        int bytes_read = recv(client_fd_, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0) {
            // Client disconnected or error — clean up
            break;
        }

        buffer[bytes_read] = '\0';
        leftover += buffer;

        // Process complete lines (delimited by \n)
        size_t pos;
        while ((pos = leftover.find('\n')) != std::string::npos) {
            std::string line = leftover.substr(0, pos + 1);
            leftover = leftover.substr(pos + 1);

            ParsedCommand cmd = CommandParser::parse(line);
            if (cmd.command.empty()) {
                continue;
            }

            std::string response = execute(cmd);
            if (!send_response(response)) {
                close_socket(client_fd_);
                return;
            }

            // QUIT command — close connection after responding
            if (cmd.command == "QUIT") {
                close_socket(client_fd_);
                return;
            }
        }
    }

    close_socket(client_fd_);
}

bool ClientHandler::send_response(const std::string& response) {
    size_t total_sent = 0;
    while (total_sent < response.size()) {
        int sent = send(
            client_fd_,
            response.data() + total_sent,
            static_cast<int>(response.size() - total_sent),
            0
        );
        if (sent <= 0) {
            return false;
        }
        total_sent += static_cast<size_t>(sent);
    }
    return true;
}

std::string ClientHandler::execute(const ParsedCommand& cmd) {
    // Dispatch based on command name.
    // Each branch validates argument count before executing.

    if (cmd.command == "PING") {
        return Protocol::pong();
    }

    if (cmd.command == "SET") {
        if (cmd.args.size() < 2) {
            return Protocol::error("wrong number of arguments for 'SET'");
        }
        store_.set(cmd.args[0], cmd.args[1]);
        return Protocol::ok();
    }

    if (cmd.command == "GET") {
        if (cmd.args.size() < 1) {
            return Protocol::error("wrong number of arguments for 'GET'");
        }
        auto val = store_.get(cmd.args[0]);
        if (val.has_value()) {
            return Protocol::value(val.value());
        }
        return Protocol::nil();
    }

    if (cmd.command == "DEL") {
        if (cmd.args.size() < 1) {
            return Protocol::error("wrong number of arguments for 'DEL'");
        }
        bool deleted = store_.del(cmd.args[0]);
        return Protocol::integer(deleted ? 1 : 0);
    }

    if (cmd.command == "EXISTS") {
        if (cmd.args.size() < 1) {
            return Protocol::error("wrong number of arguments for 'EXISTS'");
        }
        bool found = store_.exists(cmd.args[0]);
        return Protocol::integer(found ? 1 : 0);
    }

    if (cmd.command == "KEYS") {
        auto all_keys = store_.keys();
        return Protocol::array(all_keys);
    }

    if (cmd.command == "EXPIRE") {
        if (cmd.args.size() < 2) {
            return Protocol::error("wrong number of arguments for 'EXPIRE'");
        }
        try {
            int seconds = std::stoi(cmd.args[1]);
            bool success = store_.expire(cmd.args[0], seconds);
            return Protocol::integer(success ? 1 : 0);
        } catch (const std::exception&) {
            return Protocol::error("value is not an integer");
        }
    }

    if (cmd.command == "TTL") {
        if (cmd.args.size() < 1) {
            return Protocol::error("wrong number of arguments for 'TTL'");
        }
        int remaining = store_.ttl(cmd.args[0]);
        return Protocol::integer(remaining);
    }

    if (cmd.command == "SAVE") {
        bool ok = store_.save();
        return ok ? Protocol::ok() : Protocol::error("failed to save");
    }

    if (cmd.command == "QUIT") {
        return Protocol::ok();
    }

    return Protocol::error("unknown command '" + cmd.command + "'");
}
