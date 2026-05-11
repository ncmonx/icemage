#include "tool_call_cache.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace icmg::core {

static std::string fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

// Phase 76: per-agent cache namespace. ICMG_AGENT_ID env scopes cache
// entries; "" (default) = global agent (backward compat).
static std::string currentAgentId() {
    const char* a = std::getenv("ICMG_AGENT_ID");
    return a ? std::string(a) : std::string();
}

std::string ToolCallCache::makeKey(const std::string& cmd, const std::string& args) {
    // Include agent_id in key so different agents don't share cache.
    return fnv1a(currentAgentId() + std::string("\0", 1) + cmd + std::string("\0", 1) + args);
}

std::optional<std::string> ToolCallCache::lookup(const std::string& cmd,
                                                  const std::string& args_normalized) {
    std::string hash = makeKey(cmd, args_normalized);
    int64_t now = (int64_t)std::time(nullptr);
    std::optional<std::string> result;
    int64_t row_id = 0;
    try {
        db_.query("SELECT id, result_blob FROM tool_call_cache "
                  "WHERE content_hash = ? AND expires_at > ?",
                  {hash, std::to_string(now)},
                  [&](const Row& r){
                      if (r.size() >= 2) {
                          row_id = std::stoll(r[0]);
                          result = r[1];
                      }
                  });
        if (row_id > 0) {
            db_.run("UPDATE tool_call_cache SET hit_count = hit_count + 1 WHERE id = ?",
                    {std::to_string(row_id)});
        }
    } catch (...) {}
    return result;
}

void ToolCallCache::store(const std::string& cmd,
                           const std::string& args_normalized,
                           const std::string& result,
                           int ttl_sec) {
    std::string hash = makeKey(cmd, args_normalized);
    int64_t expires = (int64_t)std::time(nullptr) + ttl_sec;
    try {
        db_.run("INSERT OR REPLACE INTO tool_call_cache "
                "(cmd, content_hash, result_blob, expires_at) VALUES (?,?,?,?)",
                {cmd, hash, result, std::to_string(expires)});
    } catch (...) {}
}

int ToolCallCache::prune() {
    int64_t now = (int64_t)std::time(nullptr);
    int n = 0;
    try {
        db_.query("SELECT COUNT(*) FROM tool_call_cache WHERE expires_at <= ?",
                  {std::to_string(now)},
                  [&](const Row& r){ if (!r.empty()) n = std::stoi(r[0]); });
        db_.run("DELETE FROM tool_call_cache WHERE expires_at <= ?",
                {std::to_string(now)});
    } catch (...) {}
    return n;
}

ToolCallCache::Stats ToolCallCache::summary(int window_sec) {
    Stats s;
    int64_t cutoff = (int64_t)std::time(nullptr) - window_sec;
    try {
        db_.query("SELECT COUNT(*), COALESCE(SUM(hit_count),0) FROM tool_call_cache "
                  "WHERE created_at > ?",
                  {std::to_string(cutoff)},
                  [&](const Row& r){
                      if (r.size() >= 2) {
                          s.total = std::stoi(r[0]);
                          s.hits  = std::stoll(r[1]);
                      }
                  });
        if (s.total + s.hits > 0) {
            // hit_rate = hits / (hits + total) where total = unique entries (each one was a miss originally)
            s.hit_rate_pct = (int)(100 * s.hits / (s.hits + s.total));
        }
    } catch (...) {}
    return s;
}

} // namespace icmg::core
