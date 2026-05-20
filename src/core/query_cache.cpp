// v1.18.0: query cache impl.

#include "query_cache.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

namespace icmg::core::query_cache {

namespace {

struct Entry {
    std::string result;
    int64_t     stored_at;
};

std::mutex g_mu;
std::unordered_map<std::string, Entry> g_map;
std::atomic<size_t> g_hits{0};
std::atomic<size_t> g_misses{0};

int64_t now() {
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t ttlSeconds() {
    const char* env = std::getenv("ICMG_QUERY_CACHE_TTL_SEC");
    if (env && *env) {
        try { return std::stoll(env); } catch (...) {}
    }
    return 300;
}

}  // namespace

std::string lookup(const std::string& key) {
    int64_t n = now();
    int64_t ttl = ttlSeconds();
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_map.find(key);
    if (it == g_map.end()) { g_misses.fetch_add(1); return ""; }
    if (n - it->second.stored_at > ttl) {
        g_map.erase(it);
        g_misses.fetch_add(1);
        return "";
    }
    g_hits.fetch_add(1);
    return it->second.result;
}

void store(const std::string& key, const std::string& result) {
    if (key.empty() || result.empty()) return;
    Entry e;
    e.result = result;
    e.stored_at = now();
    std::lock_guard<std::mutex> lk(g_mu);
    g_map[key] = std::move(e);
}

void reset() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_map.clear();
    g_hits = 0;
    g_misses = 0;
}

size_t hits()   { return g_hits.load(); }
size_t misses() { return g_misses.load(); }

}  // namespace icmg::core::query_cache
