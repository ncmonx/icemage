#pragma once
// hot.hpp — me-everywhere #5: shared hot working-memory core (pure, header-only).
//
// A volatile shared whiteboard (key -> value) across live sessions on one machine:
// a scratch tier alongside the long-term archival memory (a working-brain, not a
// diary). Sessions set/read shared keys while collaborating; the doc's intent is
// "lives while the machine is on, resets on reboot" — the truly RAM-volatile tier
// arrives with the RAM-brain daemon; today this is a file-backed shared scratch
// with explicit `hot clear`. Pure serialize + last-write-wins dedup here.
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace icmg::core {

struct HotEntry {
    std::string key;
    int64_t     ts = 0;
    std::string value;
};

namespace hot_detail {
inline std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if      (c == '\\') o += "\\\\";
        else if (c == '\t') o += "\\t";
        else if (c == '\n') o += "\\n";
        else                o += c;
    }
    return o;
}
inline std::string unesc(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            o += (n == 't' ? '\t' : n == 'n' ? '\n' : n == '\\' ? '\\' : n);
        } else { o += s[i]; }
    }
    return o;
}
}  // namespace hot_detail

// One TSV line: key \t ts \t value(escaped).
inline std::string hotToLine(const HotEntry& e) {
    return e.key + "\t" + std::to_string(e.ts) + "\t" + hot_detail::esc(e.value);
}

// Parse back. false on malformed: missing fields, empty key, or non-numeric ts.
inline bool hotFromLine(const std::string& line, HotEntry& out) {
    size_t t1 = line.find('\t');         if (t1 == std::string::npos) return false;
    size_t t2 = line.find('\t', t1 + 1); if (t2 == std::string::npos) return false;
    out.key = line.substr(0, t1);
    if (out.key.empty()) return false;
    try { out.ts = std::stoll(line.substr(t1 + 1, t2 - t1 - 1)); } catch (...) { return false; }
    out.value = hot_detail::unesc(line.substr(t2 + 1));
    return true;
}

// Latest write per key wins (the append log collapses to the current whiteboard).
// Order-preserving by first appearance of each key.
inline std::vector<HotEntry> latestPerKey(const std::vector<HotEntry>& all) {
    std::vector<HotEntry> out;
    for (const auto& e : all) {
        bool found = false;
        for (auto& x : out) if (x.key == e.key) { if (e.ts >= x.ts) x = e; found = true; break; }
        if (!found) out.push_back(e);
    }
    return out;
}

}  // namespace icmg::core
