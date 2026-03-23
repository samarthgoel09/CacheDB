cmake_minimum_required(VERSION 3.10)
project(CacheDB LANGUAGES CXX)

include(CTest)
enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Platform-specific settings
if(WIN32)
    # Link Winsock on Windows
    link_libraries(ws2_32)
else()
    # Link pthreads on Unix
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
endif()

# ── Server executable ─────────────────────────────────────────────

set(SERVER_SOURCES
    src/main.cpp
    src/server.cpp
    src/client_handler.cpp
    src/store.cpp
    src/command_parser.cpp
    src/protocol.cpp
)

add_executable(cachedb ${SERVER_SOURCES})

if(NOT WIN32)
    target_link_libraries(cachedb Threads::Threads)
endif()

# ── Test client ───────────────────────────────────────────────────

add_executable(test_client tests/test_client.cpp)

if(NOT WIN32)
    target_link_libraries(test_client Threads::Threads)
endif()

# ── Core unit tests (no server required) ────────────────────────

add_executable(unit_core_tests
    tests/unit_core.cpp
    src/store.cpp
    src/command_parser.cpp
    src/protocol.cpp
)

if(NOT WIN32)
    target_link_libraries(unit_core_tests Threads::Threads)
endif()

add_test(NAME unit_core_tests COMMAND unit_core_tests)

# ── Benchmark ─────────────────────────────────────────────────────

add_executable(benchmark tests/benchmark.cpp)

if(NOT WIN32)
    target_link_libraries(benchmark Threads::Threads)
endif()
