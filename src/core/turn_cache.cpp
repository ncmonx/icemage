// v1.16.0: turn_cache impl. Thread-safe.

#include "turn_cache.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace icmg::core::turn_cache {

namespace {

struct Entry {
    std::string ref;          // "<icmg-cached:abc12345>"
    long long   recorded_at;  // unix seconds
    long long   mtime;        // file mtime when recorded (0 if N/A)
};

std::mutex g_mu;
std::unordered_map<uint64_t, Entry> g_map;
std::atomic<size_t> g_hits{0};
std::atomic<size_t> g_misses{0};

uint64_t fnv1a(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    return h;
}

uint64_t hashKey(const std::string& cmd, const std::string& args) {
    std::string concat = cmd;
    concat += '\0';
    concat += args;
    return fnv1a(concat);
}

long long now() {
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

long long ttlSeconds() {
    const char* env = std::getenv("ICMG_TURN_CACHE_TTL_SEC");
    if (env && *env) {
        try { return std::stoll(env); } catch (...) {}
    }
    return 300;  // 5 min default
}

std::string refToken(uint64_t hkey) {
    // Short hex prefix for compactness.
    char buf[32];
    std::snprintf(buf, sizeof(buf), "<icmg-cached:%08x>",
                  (unsigned)(hkey & 0xffffffff));
    return buf;
}

}  // namespace

std::string lookup(const std::string& cmd_name,
                   const std::string& args_canonical,
                   long long file_mtime_or_zero) {
    uint64_t k = hashKey(cmd_name, args_canonical);
    long long n = now();
    long long ttl = ttlSeconds();
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_map.find(k);
    if (it == g_map.end()) { g_misses.fetch_add(1); return ""; }
    if (n - it->second.recorded_at > ttl) {
        g_map.erase(it);
        g_misses.fetch_add(1);
        return "";
    }
    if (file_mtime_or_zero > 0 && file_mtime_or_zero != it->second.mtime) {
        g_map.erase(it);
        g_misses.fetch_add(1);
        return "";
    }
    g_hits.fetch_add(1);
    return it->second.ref;
}

void recordResult(const std::string& cmd_name,
                   const std::string& args_canonical,
                   long long file_mtime_or_zero,
                   const std::string& content) {
    if (content.empty()) return;
    uint64_t k = hashKey(cmd_name, args_canonical);
    Entry e;
    e.ref = refToken(k);
    e.recorded_at = now();
    e.mtime = file_mtime_or_zero;
    std::lock_guard<std::mutex> lk(g_mu);
    g_map[k] = std::move(e);
}

void resetSession() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_map.clear();
    g_hits = 0;
    g_misses = 0;
}

size_t hits()   { return g_hits.load(); }
size_t misses() { return g_misses.load(); }

}  // namespace icmg::core::turn_cache
