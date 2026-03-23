#pragma once

#include <string>
#include <vector>

/**
 * Parses raw client input into a structured command.
 *
 * Input format: "COMMAND arg1 arg2 ...\n"
 * Parsing is case-insensitive for the command name.
 */
struct ParsedCommand {
    std::string              command;  // Uppercase command name
    std::vector<std::string> args;     // Arguments (preserves original case)
};

namespace CommandParser {
    /**
     * Parse a single line of input into a command + arguments.
     * Returns a ParsedCommand with an empty command string if input is blank.
     */
    ParsedCommand parse(const std::string& input);
}
