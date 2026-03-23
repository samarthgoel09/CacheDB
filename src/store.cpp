#include "store.h"
#include <iostream>
#include <algorithm>

KVStore::KVStore(int expiry_scan_interval_ms)
    : scan_interval_ms_(expiry_scan_interval_ms)
{
    // Spawn background thread that periodically evicts expired keys.
    // This is the "active expiration" strategy — complements the lazy
    // checks we do on every get/exists call.
    expiry_thread_ = std::thread(&KVStore::expiry_loop, this);
}

KVStore::~KVStore() {
    shutdown();
}

void KVStore::shutdown() {
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false)) {
        if (expiry_thread_.joinable()) {
            expiry_thread_.join();
        }
    }
}

// ── Core CRUD ─────────────────────────────────────────────────────

std::optional<std::string> KVStore::get(const std::string& key) {
    // Shared lock: multiple readers can call get() concurrently
    std::shared_lock lock(mutex_);

    // Lazy expiration — check before returning
    if (is_expired(key)) {
        // Need to upgrade to exclusive lock to actually delete.
        // Drop shared lock, acquire unique lock, then re-check.
        lock.unlock();
        std::unique_lock ulock(mutex_);
        if (is_expired(key)) {
            data_.erase(key);
            expiry_.erase(key);
        }
        return std::nullopt;
    }

    auto it = data_.find(key);
    if (it == data_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void KVStore::set(const std::string& key, const std::string& value) {
    std::unique_lock lock(mutex_);
    data_[key] = value;
    // Setting a key clears any existing TTL — matches Redis behavior
    expiry_.erase(key);
}

bool KVStore::del(const std::string& key) {
    std::unique_lock lock(mutex_);
    expiry_.erase(key);
    return data_.erase(key) > 0;
}

bool KVStore::exists(const std::string& key) {
    std::shared_lock lock(mutex_);

    // Lazy expiration check
    if (is_expired(key)) {
        lock.unlock();
        std::unique_lock ulock(mutex_);
        if (is_expired(key)) {
            data_.erase(key);
            expiry_.erase(key);
        }
        return false;
    }

    return data_.count(key) > 0;
}

std::vector<std::string> KVStore::keys() {
    std::shared_lock lock(mutex_);
    std::vector<std::string> result;
    result.reserve(data_.size());

    auto now = Clock::now();
    for (const auto& [k, v] : data_) {
        // Skip keys that are expired but haven't been swept yet
        auto eit = expiry_.find(k);
        if (eit != expiry_.end() && eit->second <= now) {
            continue;
        }
        result.push_back(k);
    }
    return result;
}

// ── TTL / Expiry ──────────────────────────────────────────────────

bool KVStore::expire(const std::string& key, int seconds) {
    std::unique_lock lock(mutex_);
    if (data_.count(key) == 0) {
        return false;
    }
    expiry_[key] = Clock::now() + std::chrono::seconds(seconds);
    return true;
}

int KVStore::ttl(const std::string& key) {
    std::shared_lock lock(mutex_);
    if (data_.count(key) == 0) {
        return -2;  // Key does not exist
    }
    auto eit = expiry_.find(key);
    if (eit == expiry_.end()) {
        return -1;  // Key exists but has no expiry
    }

    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        eit->second - Clock::now()
    ).count();

    if (remaining <= 0) {
        return -2;  // Expired — treat as non-existent
    }
    return static_cast<int>(remaining);
}

// ── Persistence ───────────────────────────────────────────────────

bool KVStore::save(const std::string& filepath) {
    std::shared_lock lock(mutex_);

    std::ofstream out(filepath, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    // Simple format: key_len key val_len val per entry
    // Only save non-expired keys
    auto now = Clock::now();
    for (const auto& [k, v] : data_) {
        auto eit = expiry_.find(k);
        if (eit != expiry_.end() && eit->second <= now) {
            continue;  // Skip expired
        }

        uint32_t klen = static_cast<uint32_t>(k.size());
        uint32_t vlen = static_cast<uint32_t>(v.size());
        out.write(reinterpret_cast<const char*>(&klen), sizeof(klen));
        out.write(k.data(), klen);
        out.write(reinterpret_cast<const char*>(&vlen), sizeof(vlen));
        out.write(v.data(), vlen);
    }

    return out.good();
}

// ── Background Expiry ─────────────────────────────────────────────

void KVStore::expiry_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(scan_interval_ms_)
        );

        // Acquire exclusive lock and sweep expired keys
        std::unique_lock lock(mutex_);
        evict_expired_locked();
    }
}

bool KVStore::is_expired(const std::string& key) const {
    // Caller must hold at least a shared lock
    auto it = expiry_.find(key);
    if (it == expiry_.end()) {
        return false;
    }
    return it->second <= Clock::now();
}

void KVStore::evict_expired_locked() {
    // Caller must hold unique lock
    auto now = Clock::now();
    for (auto it = expiry_.begin(); it != expiry_.end(); ) {
        if (it->second <= now) {
            data_.erase(it->first);
            it = expiry_.erase(it);
        } else {
            ++it;
        }
    }
}
