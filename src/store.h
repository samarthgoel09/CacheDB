#pragma once

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <optional>
#include <vector>
#include <chrono>
#include <atomic>
#include <thread>
#include <fstream>

/**
 * Thread-safe in-memory key-value store with TTL support.
 *
 * Uses a reader-writer lock (shared_mutex) to allow concurrent reads
 * while serializing writes. Expired keys are evicted both lazily
 * (on access) and actively (via a background sweeper thread).
 */
class KVStore {
public:
    explicit KVStore(int expiry_scan_interval_ms = 1000);
    ~KVStore();

    // Disallow copy — this object owns a background thread
    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;

    // ── Core CRUD ──────────────────────────────────────────────
    std::optional<std::string> get(const std::string& key);
    void set(const std::string& key, const std::string& value);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    std::vector<std::string> keys();

    // ── TTL / Expiry ──────────────────────────────────────────
    bool expire(const std::string& key, int seconds);
    int ttl(const std::string& key);  // -1 = no expiry, -2 = key missing

    // ── Persistence ───────────────────────────────────────────
    bool save(const std::string& filepath = "dump.cdb");

    // ── Lifecycle ─────────────────────────────────────────────
    void shutdown();

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    std::unordered_map<std::string, std::string> data_;
    std::unordered_map<std::string, TimePoint>   expiry_;
    mutable std::shared_mutex                    mutex_;

    // Background expiry sweeper
    std::atomic<bool> running_{true};
    std::thread       expiry_thread_;
    int               scan_interval_ms_;

    void expiry_loop();
    bool is_expired(const std::string& key) const;  // caller must hold lock
    void evict_expired_locked();                     // caller must hold unique lock
};
