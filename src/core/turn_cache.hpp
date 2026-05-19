// v1.16.0: cross-turn unchanged-cache.
//
// Tracks (cmd, args) → result hash per resident session. On repeat with
// unchanged inputs (file mtime unchanged, args identical), emit short
// reference instead of full content. LLM uses prior-turn content.
//
// Cache key = FNV1a(cmd + "\0" + args + "\0" + extra_keys).
// Cache value = sha1 prefix of last result + emit timestamp.
// TTL: 5 min (configurable via ICMG_TURN_CACHE_TTL_SEC env).
//
// API: `cached(key)` → returns "" if fresh or first time, returns "<ref>"
// if hit. Caller emit ref instead of full content when hit.

#pragma once

#include <string>

namespace icmg::core::turn_cache {

// Returns non-empty ref string if (cmd, args, mtime) seen before this
// session AND TTL not expired. Caller emits ref. Else: empty, caller
// computes + calls `recordResult(key, content)`.
std::string lookup(const std::string& cmd_name,
                   const std::string& args_canonical,
                   long long file_mtime_or_zero = 0);

// Record content for given key. Caller invokes after computing full
// result. Stores hash + emit_timestamp.
void recordResult(const std::string& cmd_name,
                   const std::string& args_canonical,
                   long long file_mtime_or_zero,
                   const std::string& content);

// Clear all entries.
void resetSession();

// Cache hit count since last reset (for metrics).
size_t hits();
size_t misses();

}  // namespace icmg::core::turn_cache
