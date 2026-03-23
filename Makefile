# CacheDB Makefile
# Alternative build system for Linux/macOS (POSIX)

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pthread -O2
SRCDIR   := src
TESTDIR  := tests
BUILDDIR := build

# Source files
SERVER_SRCS := $(SRCDIR)/main.cpp       \
               $(SRCDIR)/server.cpp     \
               $(SRCDIR)/client_handler.cpp \
               $(SRCDIR)/store.cpp      \
               $(SRCDIR)/command_parser.cpp \
               $(SRCDIR)/protocol.cpp

# Targets
SERVER    := $(BUILDDIR)/cachedb
TEST      := $(BUILDDIR)/test_client
BENCHMARK := $(BUILDDIR)/benchmark

.PHONY: all clean server test_client benchmark

all: server test_client benchmark

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

server: $(BUILDDIR) $(SERVER_SRCS)
	$(CXX) $(CXXFLAGS) -o $(SERVER) $(SERVER_SRCS)

test_client: $(BUILDDIR) $(TESTDIR)/test_client.cpp
	$(CXX) $(CXXFLAGS) -o $(TEST) $(TESTDIR)/test_client.cpp

benchmark: $(BUILDDIR) $(TESTDIR)/benchmark.cpp
	$(CXX) $(CXXFLAGS) -o $(BENCHMARK) $(TESTDIR)/benchmark.cpp

clean:
	rm -rf $(BUILDDIR)
