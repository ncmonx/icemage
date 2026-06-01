// v1.78.2 Phase C: SQLite-backed persist ops (writeThrough + hydrate).
// Separate from pure recall_cache_persist.hpp because these touch core::Db.
//
// Production wiring (daemon):
//   - On RecallCache::put → sink lambda enqueues writeThrough(Db, scope, k, v, bytes)
//     on the WriteQueue (v1.58 F4). Non-blocking from hot path.
//   - On daemon boot → hydrate(Db, scope, 256) → for-each cache.put.

#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace icmg::core { class Db; }

namespace icmg::core {

struct HydrateEntry {
    std::string key;
    std::string value;
    std::int64_t hits = 1;
};

// UPSERT semantics: insert or replace by (scope_hash, key). hit_count
// incremented when an existing row is hit again (last_used = now).
// Returns true on success, false on DB error.
bool writeThrough(Db& db,
                  std::string_view scope_hash,
                  const std::string& key,
                  const std::string& value,
                  std::size_t byte_size);

// SELECT top-N entries for given scope_hash ordered by hit_count DESC.
// Used at daemon boot to warm the in-RAM RecallCache.
std::vector<HydrateEntry> hydrate(Db& db,
                                  std::string_view scope_hash,
                                  std::size_t cap);

}  // namespace icmg::core
