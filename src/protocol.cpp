#include "protocol.h"

namespace Protocol {

std::string ok() {
    return "+OK\n";
}

std::string error(const std::string& msg) {
    return "-ERR " + msg + "\n";
}

std::string value(const std::string& val) {
    return "$" + val + "\n";
}

std::string nil() {
    return "$NIL\n";
}

std::string integer(int val) {
    return ":" + std::to_string(val) + "\n";
}

std::string array(const std::vector<std::string>& items) {
    std::string result = "*" + std::to_string(items.size()) + "\n";
    for (const auto& item : items) {
        result += "$" + item + "\n";
    }
    return result;
}

std::string pong() {
    return "+PONG\n";
}

}  // namespace Protocol
