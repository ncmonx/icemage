#pragma once
// event_bus.hpp — me-everywhere #1: append-only action stream (pure core).
//
// The deepest leap from the presence layer: instead of just "who is alive", a
// shared append-only log of typed ACTIONS — session A emits `edit src/foo.cpp`,
// session B (tailing) sees it. Each reader keeps a cursor (the ts of the last
// event it saw) and asks for events strictly newer. This header is the pure data
// + serialize/filter logic; the shared-file append and fs-watch/daemon transport
// (carried by the RAM-brain daemon) wire on top. Mirrors presence.hpp's style.
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace icmg::core {

struct BusEvent {
    int64_t     ts = 0;      // unix seconds (also coarse ordering / reader cursor)
    std::string actor;       // emitting session_id
    std::string kind;        // edit | claim | release | done | note | ...
    std::string target;      // file / task / scope (optional)
    std::string detail;      // free text (optional)
};

namespace bus_detail {
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
        } else {
            o += s[i];
        }
    }
    return o;
}
}  // namespace bus_detail

// One TSV line: ts \t actor \t kind \t target \t detail(escaped).
inline std::string eventToLine(const BusEvent& e) {
    return std::to_string(e.ts) + "\t" + e.actor + "\t" + e.kind + "\t" +
           e.target + "\t" + bus_detail::esc(e.detail);
}

// Parse back. false on malformed: missing fields, non-numeric ts, or empty
// actor/kind (an event must have a source and a verb).
inline bool eventFromLine(const std::string& line, BusEvent& out) {
    size_t t1 = line.find('\t');             if (t1 == std::string::npos) return false;
    size_t t2 = line.find('\t', t1 + 1);     if (t2 == std::string::npos) return false;
    size_t t3 = line.find('\t', t2 + 1);     if (t3 == std::string::npos) return false;
    size_t t4 = line.find('\t', t3 + 1);     if (t4 == std::string::npos) return false;
    try { out.ts = std::stoll(line.substr(0, t1)); } catch (...) { return false; }
    out.actor  = line.substr(t1 + 1, t2 - t1 - 1);
    out.kind   = line.substr(t2 + 1, t3 - t2 - 1);
    out.target = line.substr(t3 + 1, t4 - t3 - 1);
    out.detail = bus_detail::unesc(line.substr(t4 + 1));
    if (out.actor.empty() || out.kind.empty()) return false;
    return true;
}

// Events strictly newer than the reader's cursor `since_ts`.
inline std::vector<BusEvent> eventsSince(const std::vector<BusEvent>& all, int64_t since_ts) {
    std::vector<BusEvent> out;
    for (const auto& e : all)
        if (e.ts > since_ts) out.push_back(e);
    return out;
}

}  // namespace icmg::core
