// v1.18.0: prefetch cache — preload hot data at service start.
//
// Reduces first-prompt latency by pre-loading frequently-accessed items
// into RAM during service warm-up, before any user request arrives.
//
// Caches (RAM, populated once):
//   - Hot context_nodes (top 5 by access freq) — used by SessionStart
//   - Top-50 memory entries by recency × importance — used by recall
//   - Skill manifest text — used by skill index / suggest
//
// API: `get*()` returns cached payload; empty if prefetch not yet run.
// `warm()` populates cache; called once by ServiceLoop::run().

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace icmg::core::prefetch_cache {

struct MemoryEntry {
    int64_t     id;
    std::string topic;
    std::string content;
    int64_t     created_at;
    int         importance;
};

// Run prefetch (idempotent — safe to call repeatedly). ~50-200ms one-time
// cost during service startup.
void warm();

// Accessors (thread-safe).
std::string hotContextNodes();           // concat of top-5 hot tier nodes
std::string skillManifest();             // skill manifest text
std::vector<MemoryEntry> recentMemory(); // top-50

// Invalidate — call on memory.store, context-node insert, etc.
void invalidate();

// Stats.
bool isWarm();
int64_t lastWarmedAt();

}  // namespace icmg::core::prefetch_cache
