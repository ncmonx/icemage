// v2.20 (research #1): cache-aware context assembly.
//
// Prompt caching (Anthropic -90% cost / -85% latency; OpenAI ~50% off cached
// input) only pays off when the CACHED PREFIX is byte-stable across turns. The
// naive path -- wrap the whole pack blob, with a per-task header on line 1 --
// mutates the prefix every call, so the cache never hits.
//
// This primitive segments context into STABLE (repo conventions, rules, graph
// summary, pinned facts) vs VOLATILE (task header, per-task recall, diff),
// orders all stable segments first as a reusable prefix, wraps ONLY that prefix
// in the cache sentinel, and reports a prefix hash so a caller (or the next
// turn) can tell whether the cacheable region drifted.
//
// Pure + deterministic + no LLM. Reuses cache_emitter's sentinel format.
#pragma once
#include "cache_emitter.hpp"
#include <string>
#include <vector>
#include <cstddef>

namespace icmg::cli {

enum class Volatility { Stable, Volatile };

struct ContextSegment {
    std::string label;                    // human tag (diagnostics only)
    std::string body;                     // segment content
    Volatility  vol = Volatility::Volatile;
};

struct CacheLayout {
    std::string text;                     // wrapped stable prefix + volatile tail
    std::string prefix_hash;              // FNV-1a hex of stable prefix (pre-wrap)
    std::size_t prefix_bytes   = 0;       // stable prefix size (pre-wrap)
    std::size_t tail_bytes     = 0;       // volatile tail size
    std::size_t stable_count   = 0;
    std::size_t volatile_count = 0;
    bool        wrapped        = false;   // sentinel emitted (false if no stable)
};

// Assemble segments cache-optimally. Stable segments keep their relative input
// order and form the reusable prefix; volatile segments keep their relative
// order and form the tail. Only the stable prefix is wrapped in cache
// sentinels. If there are no stable segments, nothing is wrapped (wrapped=false)
// and text is just the volatile tail.
CacheLayout assembleCacheAware(const std::vector<ContextSegment>& segs,
                               const CacheEmitOptions& opts = {});

// FNV-1a 64-bit as lowercase hex (16 chars). Self-contained, deterministic.
std::string fnv1aHex(const std::string& s);

// Split a markdown blob into segments at top-level headers (lines starting with
// "# " or "## ") and classify each: a section is STABLE when its header matches
// a known-stable topic (conventions, rules, graph/files, architecture, pinned
// facts) -- content that rarely changes between turns and forms a reusable
// cache prefix. Everything else (task header, per-task recall, diff, recent
// activity) is VOLATILE. Deterministic, case-insensitive header matching.
std::vector<ContextSegment> classifyMarkdownSections(const std::string& md);

} // namespace icmg::cli
