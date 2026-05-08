// Phase 45 T1: tool-call result cache (content-hash keyed, TTL-bound).
//
// Wraps deterministic CLI/MCP tool calls. Same cmd + same args within TTL →
// hit; skip recompute. Default TTL 300s (matches Anthropic prompt-cache window).
//
// Disable per-call via env ICMG_NO_CACHE=1 or flag --no-cache.
#pragma once
#include "db.hpp"
#include <optional>
#include <string>
#include <vector>

namespace icmg::core {

class ToolCallCache {
public:
    explicit ToolCallCache(Db& db) : db_(db) {}

    // FNV1a-64 hex of (cmd + "\0" + args_normalized).
    static std::string makeKey(const std::string& cmd, const std::string& args);

    // Returns cached result if present and not expired. Increments hit_count.
    std::optional<std::string> lookup(const std::string& cmd,
                                       const std::string& args_normalized);

    // Inserts (or replaces) cache entry.
    void store(const std::string& cmd,
               const std::string& args_normalized,
               const std::string& result,
               int ttl_sec = 300);

    // Drop expired rows.
    int  prune();

    struct Stats { int total=0; int64_t hits=0; int hit_rate_pct=0; };
    Stats summary(int window_sec = 86400);

private:
    Db& db_;
};

} // namespace icmg::core
