#pragma once
// v2.0.0 C4: lossless compaction transition. snapshotManifest records the working-set's
// node ids + the pinned subset before compaction. rebuildFromManifest re-anchors ONLY the
// pinned ids, fetched FRESH from the live source list, capped at hardCapTokens (F2: small
// cap prevents the CC thrashing-error loop where a too-large re-injection refills the
// window after each summary). Non-pinned context is NOT restored from the snapshot — the
// governor reconstructs it live next turn. Pure; persistence lives elsewhere.
#include "working_set.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace icmg::core {

struct Manifest {
    std::vector<std::string> nodeIds;
    std::vector<std::string> pinnedIds;
    std::int64_t ts = 0;
};

inline Manifest snapshotManifest(const WorkingSet& ws) {
    Manifest m;
    for (const auto& s : ws.items) {
        m.nodeIds.push_back(s.id);
        if (s.pinned) m.pinnedIds.push_back(s.id);
    }
    return m;
}

// Re-anchor pinned ids only, fetched fresh from `live`, greedily within hardCapTokens.
// Ids absent from `live` are skipped (no stale text). Non-pinned manifest ids ignored.
inline WorkingSet rebuildFromManifest(const Manifest& m, int hardCapTokens,
                                      const std::vector<Source>& live) {
    WorkingSet ws;
    int used = 0;
    for (const auto& pid : m.pinnedIds) {
        for (const auto& s : live) {
            if (s.id != pid) continue;
            if (used + s.tokens <= hardCapTokens) {
                ws.items.push_back(s);
                ws.totalTokens += s.tokens;
                used += s.tokens;
            }
            break;  // matched this pinned id
        }
    }
    return ws;
}

}  // namespace icmg::core
