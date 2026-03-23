# CacheDB

A Redis-inspired concurrent in-memory key-value store built in C++ for learning systems programming fundamentals: TCP networking, multithreading, thread safety, and memory management.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                         CacheDB                             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   ┌──────────┐    ┌──────────────────┐    ┌─────────────┐  │
│   │  Client  │───▶│  ClientHandler   │───▶│   KVStore   │  │
│   │ (TCP #1) │    │  (Thread #1)     │    │             │  │
│   └──────────┘    └──────────────────┘    │  ┌────────┐ │  │
│                                           │  │  Data  │ │  │
│   ┌──────────┐    ┌──────────────────┐    │  │  Map   │ │  │
│   │  Client  │───▶│  ClientHandler   │───▶│  └────────┘ │  │
│   │ (TCP #2) │    │  (Thread #2)     │    │             │  │
│   └──────────┘    └──────────────────┘    │  ┌────────┐ │  │
│                                           │  │ Expiry │ │  │
│   ┌──────────┐    ┌──────────────────┐    │  │  Map   │ │  │
│   │  Client  │───▶│  ClientHandler   │───▶│  └────────┘ │  │
│   │ (TCP #N) │    │  (Thread #N)     │    │             │  │
│   └──────────┘    └──────────────────┘    │  ┌────────┐ │  │
│                                           │  │ Expiry │ │  │
│   ┌──────────────────────────────────┐    │  │ Thread │ │  │
│   │         TCP Server               │    │  └────────┘ │  │
│   │  (accept loop, thread spawner)   │    └─────────────┘  │
│   └──────────────────────────────────┘                      │
│                                                             │
│   ┌───────────────┐    ┌──────────────┐    ┌────────────┐  │
│   │ CommandParser  │    │   Protocol   │    │   main()   │  │
│   │ (tokenizer)    │    │ (formatter)  │    │ (entry pt) │  │
│   └───────────────┘    └──────────────┘    └────────────┘  │
│                                                             │
└─────────────────────────────────────────────────────────────┘

Concurrency Model:
  - shared_mutex on KVStore (multiple readers OR one writer)
  - One std::thread per client connection
  - Background expiry thread for active key eviction
```

## Project Structure

```
src/
  main.cpp              -- Entry point, signal handling
  server.h/cpp          -- TCP server, accept loop, thread spawning
  client_handler.h/cpp  -- Per-client read loop, command dispatch
  store.h/cpp           -- Thread-safe KV store with TTL
  command_parser.h/cpp  -- Input tokenization
  protocol.h/cpp        -- Response formatting
tests/
  test_client.cpp       -- Functional test suite
  benchmark.cpp         -- Throughput benchmark
CMakeLists.txt          -- CMake build
Makefile                -- Alternative POSIX build
```

## Build & Run

### Using CMake (Recommended)

```bash
mkdir build && cd build
cmake ..
cmake --build .

# Start the server
./cachedb                  # default port 6379
./cachedb --port 8080      # custom port
```

### Using Make (Linux/macOS)

```bash
make all

./build/cachedb
```

### On Windows (Visual Studio / MSVC)

```powershell
mkdir build; cd build
cmake ..
cmake --build . --config Release

.\Release\cachedb.exe
```

## Supported Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `PING` | `PING` | Returns `+PONG` — health check |
| `SET` | `SET key value` | Store a key-value pair |
| `GET` | `GET key` | Retrieve value by key |
| `DEL` | `DEL key` | Delete a key |
| `EXISTS` | `EXISTS key` | Check if key exists (returns 1/0) |
| `KEYS` | `KEYS` | List all non-expired keys |
| `EXPIRE` | `EXPIRE key seconds` | Set TTL on a key |
| `TTL` | `TTL key` | Get remaining TTL (-1 = no expiry, -2 = missing) |
| `SAVE` | `SAVE` | Snapshot store to disk (`dump.cdb`) |
| `QUIT` | `QUIT` | Close client connection |

### Example Session

```
$ telnet localhost 6379
PING
+PONG
SET user:1 alice
+OK
GET user:1
$alice
EXPIRE user:1 60
:1
TTL user:1
:59
DEL user:1
:1
GET user:1
$NIL
QUIT
+OK
```

## Testing

```bash
# Start the server in one terminal
./cachedb

# Run the test suite in another terminal
./test_client

# Run the benchmark
./benchmark                          # defaults: 4 threads, 1000 ops each
./benchmark 127.0.0.1 6379 8 5000   # custom: 8 threads, 5000 ops each
```

### Sample Benchmark Output

```
════════════════════════════════════════════
  CacheDB Benchmark
════════════════════════════════════════════
  Server:          127.0.0.1:6379
  Threads:         4
  Ops/thread:      1000 SET + 1000 GET
  Total ops:       8000
────────────────────────────────────────────

  Results:
  ────────────────────────────────────────
  Completed ops:   8000
  Elapsed time:    1523.45 ms
  Throughput:      5251 ops/sec
════════════════════════════════════════════
```

## Technical Highlights

### Thread Safety
- **Reader-writer lock** (`std::shared_mutex`): multiple threads can read concurrently, writes acquire exclusive access
- Lock upgrade pattern: if a read discovers an expired key, it drops the shared lock and re-acquires exclusively to evict

### TTL / Expiration
- **Lazy expiration**: checked on every `GET`, `EXISTS` call — if the key is expired, it's deleted before returning
- **Active expiration**: background thread sweeps the expiry map every second, evicting stale keys even if no client is reading them
- Matches Redis's hybrid approach

### Networking
- **POSIX sockets** (Linux/macOS) + **Winsock2** (Windows) — fully cross-platform
- `SO_REUSEADDR` to avoid "address already in use" on restart
- `SIGPIPE` ignored on POSIX so writes to dead connections don't crash the server

### Resource Management
- **RAII everywhere**: server socket closed in destructor, Winsock init/cleanup scoped
- **No raw `new`/`delete`**: all ownership is through stack objects and standard containers
- Graceful shutdown via `SIGINT`/`SIGTERM` signal handlers

## What I Learned

1. **Thread Safety**: Why `std::mutex` isn't enough for read-heavy workloads, and how `shared_mutex` enables concurrent readers. The subtlety of lock upgrades (you can't atomically upgrade shared → exclusive — you must drop and re-acquire, introducing a TOCTOU window that requires re-checking).

2. **TCP Networking**: TCP is a stream protocol — there are no message boundaries. You must implement your own framing (here, newline-delimited). `recv()` can return partial data or multiple messages concatenated.

3. **Memory Management**: RAII makes resource cleanup automatic. By putting the socket in a class with a destructor, and using `std::thread` (which is RAII for OS threads), there's no cleanup code to forget.

4. **System Design**: Separation of concerns matters. The store knows nothing about networking. The server knows nothing about commands. Each layer is independently testable and replaceable.

5. **Expiration Strategies**: Lazy expiration alone means memory leaks (expired keys sit forever if never accessed). Active expiration alone means latency spikes during sweeps. The hybrid approach balances memory usage and latency.

## License

MIT
