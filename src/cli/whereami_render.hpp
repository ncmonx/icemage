#pragma once
// Pure renderer for `icmg whereami` — the "ground truth" report (2026-06-11).
//
// Born from real friction: this session lost time to (a) a stale ~/bin binary
// reporting an old version, (b) config persisting to %APPDATA% while ~/.icmg
// was inspected, (c) which-binary-is-on-PATH ambiguity. `icmg whereami` prints
// one authoritative snapshot. Kept pure (no I/O) so the formatting is unit-
// testable; the command gathers the rows then calls renderWhereAmI.

#include <string>
#include <utility>
#include <vector>

namespace icmg::cli {

// Render label/value rows as left-aligned "label : value" lines. Labels are
// padded to the widest label so values line up. Empty value -> "(unknown)".
inline std::string renderWhereAmI(const std::vector<std::pair<std::string, std::string>>& rows) {
    size_t w = 0;
    for (const auto& r : rows) if (r.first.size() > w) w = r.first.size();
    std::string out;
    for (const auto& r : rows) {
        out += r.first;
        for (size_t i = r.first.size(); i < w; ++i) out += ' ';
        out += " : ";
        out += r.second.empty() ? "(unknown)" : r.second;
        out += "\n";
    }
    return out;
}

}  // namespace icmg::cli
