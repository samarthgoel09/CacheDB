#include "command_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace CommandParser {

ParsedCommand parse(const std::string& input) {
    ParsedCommand cmd;

    // Strip trailing \r\n or \n
    std::string trimmed = input;
    while (!trimmed.empty() &&
           (trimmed.back() == '\n' || trimmed.back() == '\r')) {
        trimmed.pop_back();
    }

    if (trimmed.empty()) {
        return cmd;
    }

    // Tokenize by whitespace
    std::istringstream stream(trimmed);
    std::string token;

    // First token is the command — convert to uppercase
    if (stream >> token) {
        std::transform(token.begin(), token.end(), token.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::toupper(c));
                       });
        cmd.command = std::move(token);
    }

    // Remaining tokens are arguments (preserve case)
    while (stream >> token) {
        cmd.args.push_back(std::move(token));
    }

    return cmd;
}

}  // namespace CommandParser
