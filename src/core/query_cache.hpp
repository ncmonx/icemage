// v1.18.0: query result cache — (query_hash → result string) with TTL.
//
// Used by BM25 recall + context-node match to short-circuit repeat queries
// within session. TTL configurable via ICMG_QUERY_CACHE_TTL_SEC (default
// 300s).
//
// Distinct from turn_cache (which is per-cmd file-aware) and inject_dedup
// (which is per-hook envelope hash). query_cache focuses on text search
// queries that may repeat across multiple hook fires.

#pragma once

#include <string>

namespace icmg::core::query_cache {

// Return cached result if present + not expired; else empty.
std::string lookup(const std::string& key);

// Store result with current timestamp. Key = caller-built (typically
// "cmd:args:flags").
void store(const std::string& key, const std::string& result);

// Clear all entries.
void reset();

// Stats.
size_t hits();
size_t misses();

}  // namespace icmg::core::query_cache
