#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "../src/command_parser.h"
#include "../src/protocol.h"
#include "../src/store.h"

static int passed = 0;
static int failed = 0;

#define CHECK(name, cond)                                                  \
    do {                                                                    \
        if (cond) {                                                         \
            std::cout << "[PASS] " << name << std::endl;                  \
            ++passed;                                                       \
        } else {                                                            \
            std::cout << "[FAIL] " << name << std::endl;                  \
            ++failed;                                                       \
        }                                                                   \
    } while (0)

int main() {
    {
        ParsedCommand cmd = CommandParser::parse("set myKey Value\r\n");
        CHECK("parse uppercases command", cmd.command == "SET");
        CHECK("parse keeps args", cmd.args.size() == 2 && cmd.args[0] == "myKey" && cmd.args[1] == "Value");
    }

    {
        CHECK("protocol ok", Protocol::ok() == "+OK\n");
        CHECK("protocol error", Protocol::error("bad") == "-ERR bad\n");
        CHECK("protocol integer", Protocol::integer(42) == ":42\n");
        CHECK("protocol array", Protocol::array({"a", "b"}) == "*2\n$a\n$b\n");
    }

    {
        KVStore store(20);
        store.set("k", "v");
        CHECK("store get", store.get("k").has_value() && store.get("k").value() == "v");
        CHECK("store exists", store.exists("k"));
        CHECK("store ttl without expiry", store.ttl("k") == -1);
        CHECK("store expire existing", store.expire("k", 1));

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));

        CHECK("store expires key", !store.get("k").has_value());
        CHECK("store ttl missing", store.ttl("k") == -2);
        CHECK("store del missing", !store.del("k"));
        store.shutdown();
    }

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed == 0 ? 0 : 1;
}
