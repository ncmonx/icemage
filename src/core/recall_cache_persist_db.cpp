// v1.78.2 Phase C: SQLite-backed persist ops impl.
//
// Schema (migration 0033, global DB):
//   recall_cache_persist(scope_hash TEXT, key TEXT, value BLOB,
//                        hit_count INT, last_used INT, byte_size INT,
//                        PRIMARY KEY (scope_hash, key))

#include "recall_cache_persist_db.hpp"
#include "db.hpp"

#include <chrono>

namespace icmg::core {

bool writeThrough(Db& db,
                  std::string_view scope_hash,
                  const std::string& key,
                  const std::string& value,
                  std::size_t byte_size) {
    try {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
        // INSERT or update hit_count + last_used on conflict.
        // SQLite ON CONFLICT (scope_hash,key) → upsert.
        db.run(
            "INSERT INTO recall_cache_persist "
            "(scope_hash, key, value, hit_count, last_used, byte_size) "
            "VALUES (?, ?, ?, 1, ?, ?) "
            "ON CONFLICT(scope_hash, key) DO UPDATE SET "
            "value = excluded.value, "
            "hit_count = hit_count + 1, "
            "last_used = excluded.last_used, "
            "byte_size = excluded.byte_size",
            {std::string(scope_hash), key, value, std::to_string(now),
             std::to_string(byte_size)});
        return true;
    } catch (...) {
        // Best-effort: cache continues serving from RAM even if persist fails.
        return false;
    }
}

std::vector<HydrateEntry> hydrate(Db& db,
                                  std::string_view scope_hash,
                                  std::size_t cap) {
    std::vector<HydrateEntry> out;
    out.reserve(cap);
    try {
        db.query(
            "SELECT key, value, hit_count FROM recall_cache_persist "
            "WHERE scope_hash = ? "
            "ORDER BY hit_count DESC, last_used DESC "
            "LIMIT ?",
            {std::string(scope_hash), std::to_string(cap)},
            [&](const Row& r) {
                if (r.size() < 3) return;
                HydrateEntry e;
                e.key = r[0];
                e.value = r[1];
                try { e.hits = std::stoll(r[2]); } catch (...) { e.hits = 1; }
                out.push_back(std::move(e));
            });
    } catch (...) {
        // Best-effort: return whatever we got before the error (likely empty).
    }
    return out;
}

}  // namespace icmg::core
