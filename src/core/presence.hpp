#pragma once
// presence.hpp — me-everywhere step 1: presence core (pure, header-only).
//
// "Who is alive on this machine, on what, NOW." Multiple icmg sessions run in
// parallel; today they only sync via written traces read at wake. Presence adds
// live mutual-awareness: each session beats a heartbeat with its current focus to
// a shared file; any session can read who is live (heartbeat within TTL) and what
// they hold. This header is the PURE data + liveness/merge/serialize logic
// (mirrors agent_lease's stale-by-heartbeat pattern); the shared-file storage and
// the heartbeat hook are thin wiring on top. Carrier for the deeper event bus =
// the RAM-brain daemon (v1.78.3).
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace icmg::core {

struct PresenceEntry {
    std::string session_id;       // stable id for this session
    int64_t     pid = 0;
    std::string focus;            // current file/task held (free text)
    int64_t     heartbeat_at = 0; // unix seconds of last beat
};

// Live entries: heartbeat within ttl (inclusive). now/ttl in seconds.
inline std::vector<PresenceEntry> livePresence(const std::vector<PresenceEntry>& all,
                                               int64_t now, int64_t ttl_sec) {
    std::vector<PresenceEntry> out;
    for (const auto& e : all)
        if (now - e.heartbeat_at <= ttl_sec) out.push_back(e);
    return out;
}

// Upsert by session_id: replace the entry with the same id, else append.
inline std::vector<PresenceEntry> upsertPresence(std::vector<PresenceEntry> all,
                                                 const PresenceEntry& e) {
    for (auto& x : all)
        if (x.session_id == e.session_id) { x = e; return all; }
    all.push_back(e);
    return all;
}

// Collapse an append-only beat log to one entry per session (the latest
// heartbeat wins). Order-preserving by first appearance. Lets writers just
// append their beat line (no read-modify-write race); readers dedup here.
inline std::vector<PresenceEntry> latestPerSession(const std::vector<PresenceEntry>& all) {
    std::vector<PresenceEntry> out;
    for (const auto& e : all) {
        bool found = false;
        for (auto& x : out)
            if (x.session_id == e.session_id) { if (e.heartbeat_at >= x.heartbeat_at) x = e; found = true; break; }
        if (!found) out.push_back(e);
    }
    return out;
}

// Serialize to one TSV line: id \t pid \t heartbeat_at \t focus(escaped).
// focus may contain tabs/newlines -> escaped so the line round-trips.
inline std::string presenceToLine(const PresenceEntry& e) {
    std::string esc;
    for (char c : e.focus) {
        if      (c == '\\') esc += "\\\\";
        else if (c == '\t') esc += "\\t";
        else if (c == '\n') esc += "\\n";
        else                esc += c;
    }
    return e.session_id + "\t" + std::to_string(e.pid) + "\t" +
           std::to_string(e.heartbeat_at) + "\t" + esc;
}

// Parse a TSV line back. Returns false on a malformed line (missing fields or
// non-numeric pid/heartbeat).
inline bool presenceFromLine(const std::string& line, PresenceEntry& out) {
    size_t t1 = line.find('\t');                 if (t1 == std::string::npos) return false;
    size_t t2 = line.find('\t', t1 + 1);         if (t2 == std::string::npos) return false;
    size_t t3 = line.find('\t', t2 + 1);         if (t3 == std::string::npos) return false;
    std::string id  = line.substr(0, t1);
    std::string pid = line.substr(t1 + 1, t2 - t1 - 1);
    std::string hb  = line.substr(t2 + 1, t3 - t2 - 1);
    std::string fe  = line.substr(t3 + 1);
    if (id.empty()) return false;
    try {
        out.pid          = std::stoll(pid);
        out.heartbeat_at = std::stoll(hb);
    } catch (...) { return false; }
    out.session_id = id;
    // unescape focus
    std::string f;
    for (size_t i = 0; i < fe.size(); ++i) {
        if (fe[i] == '\\' && i + 1 < fe.size()) {
            char n = fe[++i];
            if      (n == '\\') f += '\\';
            else if (n == 't')  f += '\t';
            else if (n == 'n')  f += '\n';
            else                f += n;
        } else {
            f += fe[i];
        }
    }
    out.focus = f;
    return true;
}

}  // namespace icmg::core
