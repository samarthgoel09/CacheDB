# CacheDB

CacheDB is a small Redis-style in-memory key-value server written in C++.

It was built to practice:
- TCP server programming
- multi-threaded request handling
- thread-safe shared state
- TTL/expiration logic

## Features

- Text protocol over TCP (default port 6379)
- One thread per client connection
- Thread-safe key-value store with std::shared_mutex
- Key expiration with lazy + background cleanup
- Basic persistence with SAVE (writes dump.cdb)

## Project Layout

```text
src/
  main.cpp
  server.h/cpp
  client_handler.h/cpp
  store.h/cpp
  command_parser.h/cpp
  protocol.h/cpp

tests/
  test_client.cpp   # integration test client (server must be running)
  unit_core.cpp     # standalone unit-style tests (CTest)
  benchmark.cpp
```

## Build

### CMake (recommended)

```bash
cmake -S . -B build
cmake --build build
```

### Windows (MSVC)

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

## Run

```bash
# Linux/macOS
./build/cachedb

# Windows
.\build\Debug\cachedb.exe
```

Optional port:

```bash
./build/cachedb --port 8080
```

## Test

### Core tests (CTest)

```bash
ctest --test-dir build -C Debug --output-on-failure
```

### Integration test client

Start server in one terminal, then run in another:

```bash
# Linux/macOS
./build/test_client 127.0.0.1 6379

# Windows
.\build\Debug\test_client.exe 127.0.0.1 6379
```

## Supported Commands

| Command | Example | Meaning |
|---|---|---|
| PING | PING | health check |
| SET | SET key value | set value |
| GET | GET key | get value |
| DEL | DEL key | delete key |
| EXISTS | EXISTS key | 1 if present, else 0 |
| KEYS | KEYS | list non-expired keys |
| EXPIRE | EXPIRE key 60 | set TTL in seconds |
| TTL | TTL key | remaining TTL (-1 no TTL, -2 missing) |
| SAVE | SAVE | write snapshot to dump.cdb |
| QUIT | QUIT | close connection |

## Notes and Limitations

- Protocol is newline-delimited plain text (not RESP).
- Persistence is minimal and write-only in current form.
- No authentication or clustering.
- Designed as a learning project, not production-ready.

## License

MIT
