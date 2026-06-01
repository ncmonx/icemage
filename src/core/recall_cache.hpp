// ram-brain: in-RAM hot cache for recall results. Process-agnostic (same class
// used in-process and inside the daemon). LRU + TTL + byte cap + pin. The cache
// is read-through/derived — disk SQLite stays the durable source of truth.
#pragma once
#include <cstdint>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

// M8 T1: djb2 hash for LRU cache map keys.
// ~100x faster than SHA256, runtime-stable (no ASLR randomization).
struct Djb2Hash {
    std::size_t operator()(const std::string& s) const noexcept {
        std::size_t h = 5381;
        for (unsigned char c : s) h = ((h << 5) + h) ^ c;
        return h;
    }
};



namespace icmg { namespace core {

struct CacheStats {
    std::uint64_t hits = 0, misses = 0, evictions = 0;
    std::size_t entries = 0, bytes = 0, cap_entries = 0, cap_bytes = 0;
};

class RecallCache {
public:
    void setCap(std::size_t max_entries, std::size_t max_bytes);
    void setTtlSeconds(std::int64_t ttl) { ttl_ = ttl; }
    using PersistSink = std::function<void(const std::string& key, const std::string& value, std::size_t bytes)>;
    void setPersistSink(PersistSink s) { sink_ = std::move(s); }

    // Production clock (std::time).
    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, const std::string& value);

    // Deterministic-clock variants for tests.
    std::optional<std::string> getAt(const std::string& key, std::int64_t now_sec);
    void putAt(const std::string& key, const std::string& value, std::int64_t now_sec);

    void flush();
    void pin(const std::string& key);
    void pinHot(std::size_t topN);
    void evictToFit();
    CacheStats stats() const;

private:
    struct Entry {
        std::string key, value;
        std::size_t bytes;
        std::uint64_t hits;
        std::int64_t last_used;   // for LRU recency (refreshed on get)
        std::int64_t created;     // for TTL (insertion time; NOT refreshed)
        bool pinned;
    };
    std::list<Entry> lru_;   // front = MRU
    std::unordered_map<std::string, std::list<Entry>::iterator, Djb2Hash> map_; // M8 T1
    std::size_t cap_entries_ = 256, cap_bytes_ = 16u << 20, bytes_ = 0;
    std::int64_t ttl_ = 300;
    PersistSink   sink_;
    CacheStats agg_{};       // cumulative hits/misses/evictions
    void touch(std::list<Entry>::iterator it, std::int64_t now);
    void evictLRUUnpinned();
};

// Pure governor sizing: given available + total RAM (MB) and current cache
// bytes, return the target byte cap with hysteresis (shrink ≥85% used, grow
// ≤60% used), clamped to [floor_bytes, ceil_bytes]. total_mb==0 -> floor.
std::size_t governorTargetBytes(std::uint64_t avail_ram_mb, std::size_t cur_bytes,
                                std::size_t floor_bytes, std::size_t ceil_bytes,
                                std::uint64_t total_ram_mb);

// Pure tick body: resize cap from RAM numbers, pin hottest, evict to fit.
void runGovernorOnce(RecallCache& c, std::uint64_t avail_mb, std::uint64_t total_mb);

}} // namespace
