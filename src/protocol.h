#pragma once

#include <string>
#include <vector>

/**
 * Response formatting layer — translates internal results into
 * the wire protocol sent back to clients.
 *
 * Protocol:
 *   +OK\n              — success acknowledgement
 *   $value\n           — value response
 *   $NIL\n             — null / key not found
 *   -ERR message\n     — error response
 *   *N\n $v1\n $v2\n   — array response (for KEYS)
 *   :N\n               — integer response (for TTL)
 */
namespace Protocol {
    std::string ok();
    std::string error(const std::string& msg);
    std::string value(const std::string& val);
    std::string nil();
    std::string integer(int val);
    std::string array(const std::vector<std::string>& items);
    std::string pong();
}
