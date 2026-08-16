# CacheDB

An in-memory key-value server in C++17, speaking a Redis-style text protocol over TCP.
Reader-writer locking lets reads run concurrently while writes serialize; measured
throughput peaks at **~177,000 operations/second across 16 concurrent clients** on
loopback, scaling near-linearly to 4 clients before contention takes over.

## What it does

Listens on a TCP port and serves ten commands over a newline-delimited text protocol.
Each connection gets its own handler thread; all of them share one `KVStore` guarded by
a `std::shared_mutex`. Keys can carry a TTL, and expired keys are removed by two
independent mechanisms — lazily when something touches the key, and actively by a
background sweeper thread. `SAVE` writes a length-prefixed binary snapshot.

| Command | Example | Returns |
|---|---|---|
| `PING` | `PING` | `+PONG` |
| `SET` | `SET key value` | `+OK` |
| `GET` | `GET key` | `$value`, or `$NIL` if absent |
| `DEL` | `DEL key` | `:1` if deleted, `:0` if absent |
| `EXISTS` | `EXISTS key` | `:1` / `:0` |
| `KEYS` | `KEYS` | `*n` followed by `$key` per line |
| `EXPIRE` | `EXPIRE key 60` | `:1` if the key exists, `:0` otherwise |
| `TTL` | `TTL key` | seconds, `:-1` if no TTL, `:-2` if absent |
| `SAVE` | `SAVE` | `+OK`, writes `dump.cdb` |
| `QUIT` | `QUIT` | `+OK`, then closes the connection |

## Architecture

```
main.cpp          argument parsing, SIGINT/SIGTERM handlers
  └─ Server       listen/bind/accept loop, thread per connection
       └─ ClientHandler   per-connection recv loop, line framing, dispatch
            ├─ CommandParser   tokenize, uppercase the verb
            ├─ Protocol        response formatting
            └─ KVStore   ← shared by every connection, shared_mutex guarded
                 └─ background sweeper thread
```

TCP delivers a byte stream with no message boundaries, so `ClientHandler` accumulates
into a `leftover` buffer and only dispatches once it sees a `\n`, carrying any partial
line into the next `recv`. `send_response` loops until the whole response is written
rather than assuming one `send` suffices.

## Concurrency and correctness

`KVStore` holds two `unordered_map`s — values and expiry deadlines — under a single
`std::shared_mutex`. Reads (`get`, `exists`, `keys`, `ttl`, `save`) take a shared lock
and run concurrently. Writes (`set`, `del`, `expire`, and the sweeper) take an exclusive
lock.

The interesting case is lazy expiration on a read path. `get` discovers an expired key
while holding only a shared lock, but erasing it requires an exclusive one, and
`shared_mutex` has no atomic upgrade. The code drops the shared lock, acquires the
exclusive lock, and **re-checks the expiry before erasing** (`store.cpp:34-44`):

```cpp
std::shared_lock lock(mutex_);
if (is_expired(key)) {
    lock.unlock();
    std::unique_lock ulock(mutex_);
    if (is_expired(key)) {        // re-check: another thread may have
        data_.erase(key);         // refreshed or removed the key in the gap
        expiry_.erase(key);
    }
    return std::nullopt;
}
```

Without the inner re-check, two threads could both observe the key as expired, and the
second would erase a value the first had already replaced. The re-check closes that
window, and the erase is correct.

### A narrower race remains, on the return value

The re-check guards the *erase* but not the *return*. `return std::nullopt` is
unconditional, so it executes even when the re-check found the key alive:

| | Thread A — `GET k` | Thread B — `SET k v2` |
|---|---|---|
| 1 | shared lock; `is_expired(k)` → true | |
| 2 | `lock.unlock()` | |
| 3 | | exclusive lock; `data_[k] = v2`; `expiry_.erase(k)` |
| 4 | exclusive lock; `is_expired(k)` → **false** | |
| 5 | erase correctly skipped | |
| 6 | **returns `nullopt`** — but `k` holds `v2` | |

A `GET` issued after a successful `SET` can return `$NIL`. The store is not corrupted
and nothing is lost — the value is intact and the next `GET` finds it — but a client can
observe a spurious miss. `exists` has the identical shape (`store.cpp:70-78`) and
returns `false` on the same path. Closing it means re-reading the map under the
exclusive lock and returning what is actually there instead of returning early.

This is documented rather than fixed; the repository is a record of what was submitted.

### Other concurrency notes

- `Server::client_threads_` grows without bound. Every accepted connection appends a
  `std::thread` and nothing removes finished ones, so a long-lived server accumulates
  thread objects for every connection it has ever served (`server.cpp:131`).
- `Server::cleanup` detaches client threads instead of joining them, so handler threads
  can outlive the `Server` — and therefore the `KVStore` they hold a reference to.
- The sweeper thread sleeps for its full interval before re-checking the shutdown flag,
  so `shutdown()` blocks for up to `scan_interval_ms_` (default 1000 ms). A condition
  variable would make it immediate.

## Benchmarks

`tests/benchmark.cpp` spawns N client threads, each on its own connection, each issuing
1000 `SET` then 1000 `GET` against distinct keys, then deleting them.

Median of 3 runs, range in parentheses:

| Client threads | Throughput (ops/sec) | Scaling vs 1 thread |
|---|---|---|
| 1 | 20,306 (19,723 – 20,721) | 1.00× |
| 2 | 40,281 (39,205 – 40,496) | 1.98× |
| 4 | 79,198 (79,025 – 80,461) | 3.90× |
| 8 | 113,351 (103,615 – 113,419) | 5.58× |
| **16** | **176,836 (176,207 – 185,258)** | **8.71×** |
| 32 | 170,696 (160,037 – 186,128) | 8.41× |
| 64 | 159,454 (150,014 – 163,510) | 7.85× |

**Methodology.** AMD Ryzen 9 270, 8 physical / 16 logical cores, 15.3 GB RAM,
Windows 11 (build 26200). GCC 14.2.0, CMake 4.2.3, `-DCMAKE_BUILD_TYPE=Release`. Client
and server on the same host over the loopback interface, so these numbers include no
network. Keys are `bench:<thread>:<i>` (~12 bytes), values `value_<i>` (~9 bytes) — all
small, all distinct, so there is no key contention between threads and the write lock is
never held long. Operation mix is 50/50 SET/GET.

Two things this measurement is not. It is a single-node loopback figure and is not
comparable to a tuned production Redis deployment. And it is throughput at saturation,
not latency under load — the benchmark reports no percentiles.

**The reported figure is conservative by roughly a third.** Each worker also deletes its
1000 keys before exiting, and that cleanup runs inside the timed region while
contributing nothing to the operation count (`benchmark.cpp:103-107` and `136-157`). The
timer therefore covers ~3000 round-trips per thread but the counter records 2000.
Measured as round-trips rather than counted operations, the 16-thread figure is closer to
265,000/sec.

Peak throughput lands at 16 client threads, matching the machine's 16 logical cores;
past that, thread-per-connection scheduling and write-lock contention outweigh the added
concurrency.

Build type makes no measurable difference. Alternating Debug and Release at 4 threads,
five runs each, every sample from both fell in 73,869 – 85,513 ops/sec with overlapping
medians — the workload is bound by socket round-trip latency, not by anything the
optimizer can reach.

## Running it

### Docker

```bash
docker build -t cachedb . && docker run --rm -p 6379:6379 cachedb
```

Snapshots go to `/data` in the container; mount a volume there to keep them:
`-v cachedb-data:/data`.

### Build from source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/cachedb                 # Windows: .\build\cachedb.exe
./build/cachedb --port 8080     # optional port override
```

### Talk to it

```bash
$ printf 'SET greeting hello\nGET greeting\nEXPIRE greeting 60\nTTL greeting\nQUIT\n' \
    | nc 127.0.0.1 6379
+OK
$hello
:1
:59
+OK
```

`TTL` reports 59 rather than 60 because the remaining time is cast to whole seconds and
truncated, and a fraction of a second has already elapsed since `EXPIRE`.

## Testing

34 assertions across two binaries. All 34 pass on the configuration in Methodology
above.

| Suite | Assertions | Needs a server? | Covers |
|---|---|---|---|
| `tests/unit_core.cpp` | 13 | no | parser casing and argument splitting, all six response formats, store CRUD, TTL, real-time expiry |
| `tests/test_client.cpp` | 21 | yes | all ten commands end to end, expiry over the wire, argument-count and unknown-command errors |

```bash
ctest --test-dir build --output-on-failure        # unit_core, registered with CTest
./build/cachedb &                                  # then, in another shell:
./build/test_client 127.0.0.1 6379
```

Two gaps worth naming. Nothing tests the concurrent paths — every assertion is
single-threaded, so the race described above would not be caught by this suite. And
`test_client.cpp:185` asserts `TTL returns positive value` by checking only that the
response begins with `:`, which `:-1` and `:-2` also satisfy; it does not test what its
name says.

## Limitations

- **A value equal to `NIL` is indistinguishable from a missing key.** `Protocol::nil()`
  and `Protocol::value("NIL")` both produce `$NIL\n`. Verified against a running server:
  `SET k NIL` then `GET k` returns exactly what `GET missing_key` returns.
- **Values cannot contain spaces.** The parser splits on whitespace and `SET` reads only
  `args[0]` and `args[1]`, so `SET k hello world` stores `hello` and discards `world`
  without reporting an error.
- **Values cannot contain newlines** — the protocol is newline-framed with no escaping
  or length prefix.
- Plain text, not RESP; existing Redis clients will not work against it.
- `SAVE` writes a snapshot but nothing loads it back, so persistence is write-only.
- `SAVE` holds the shared lock across the whole file write, blocking writers for its
  duration.
- One OS thread per connection, with no cap and no pooling.
- No authentication, no TLS, no replication, no clustering.
- `KEYS` walks the entire keyspace under a shared lock.

## License

MIT
