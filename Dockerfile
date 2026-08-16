# ── Build ─────────────────────────────────────────────────────────
FROM gcc:14 AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends cmake \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY tests/ ./tests/

# Static libstdc++/libgcc so the runtime image needs no matching GCC runtime.
RUN cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
 && cmake --build build -j"$(nproc)"

# ── Runtime ───────────────────────────────────────────────────────
# Must match the build stage's Debian release: gcc:14 is built on trixie, and
# glibc is still linked dynamically. Pairing it with bookworm-slim (glibc 2.36
# vs trixie's 2.41) fails at startup with a "GLIBC_2.4x not found" loader error.
FROM debian:trixie-slim

RUN useradd --system --uid 10001 cachedb \
 && mkdir -p /data \
 && chown cachedb:cachedb /data

COPY --from=build /src/build/cachedb /usr/local/bin/cachedb

USER cachedb

# SAVE writes dump.cdb relative to the working directory.
# Mount a volume here to persist snapshots: -v cachedb-data:/data
WORKDIR /data

EXPOSE 6379
ENTRYPOINT ["cachedb"]
CMD ["--port", "6379"]
