// v1.79.0 ICM dual-memory: see atom_store.hpp
#include "atom_store.hpp"
#include "atom_split.hpp"
#include <unordered_set>
#include <string>

namespace icmg::imem {

void AtomStore::enqueue(int64_t node_id, int64_t now_sec) {
    // One pending row per node (idempotent re-enqueue preserves attempts).
    db_.run("INSERT OR REPLACE INTO memory_atom_queue(node_id,enqueued_at,attempts) "
            "VALUES(?, ?, COALESCE((SELECT attempts FROM memory_atom_queue WHERE node_id=?),0))",
            { std::to_string(node_id), std::to_string(now_sec), std::to_string(node_id) });
}

int AtomStore::drainQueue(int max) {
    // collect pending node ids (oldest first)
    std::vector<int64_t> ids;
    db_.query("SELECT node_id FROM memory_atom_queue ORDER BY enqueued_at LIMIT ?",
              { std::to_string(max) },
              [&](const core::Row& r) { if (!r.empty()) ids.push_back(std::stoll(r[0])); });

    int processed = 0;
    for (int64_t id : ids) {
        std::string content, zone = "default";
        // memory_nodes.deleted_at is nullable (no default) — NULL means live.
        db_.query("SELECT content, zone FROM memory_nodes WHERE id=? AND COALESCE(deleted_at,0)=0",
                  { std::to_string(id) },
                  [&](const core::Row& r) {
                      if (r.size() >= 2) { content = r[0]; zone = r[1]; }
                  });

        if (!content.empty()) {
            auto atoms = atomSplit(content);
            std::unordered_set<std::string> seen;
            for (auto& a : atoms) {
                if (!seen.insert(a).second) continue;          // dedup within source
                db_.run("INSERT INTO memory_atoms"
                        "(source_node_id,content,keywords,zone,created_at) VALUES(?,?,?,?,?)",
                        { std::to_string(id), a, std::string(), zone, "0" });
                int64_t rid = db_.lastInsertId();
                db_.run("INSERT INTO memory_atoms_fts(rowid,content,keywords) VALUES(?,?,'')",
                        { std::to_string(rid), a });
            }
        }
        db_.run("DELETE FROM memory_atom_queue WHERE node_id=?", { std::to_string(id) });
        ++processed;
    }
    return processed;
}

std::vector<int64_t> AtomStore::recallAtomSources(const std::string& query, int limit) {
    std::vector<int64_t> out;
    std::unordered_set<int64_t> seen;
    db_.query(
        "SELECT a.source_node_id FROM memory_atoms_fts f "
        "JOIN memory_atoms a ON a.id=f.rowid "
        "WHERE memory_atoms_fts MATCH ? AND a.deleted_at=0 "
        "ORDER BY rank LIMIT ?",
        { query, std::to_string(limit * 4) },
        [&](const core::Row& r) {
            if (r.empty() || (int)out.size() >= limit) return;
            int64_t sid = std::stoll(r[0]);
            if (seen.insert(sid).second) out.push_back(sid);
        });
    return out;
}

} // namespace icmg::imem
