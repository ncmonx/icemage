#pragma once
// msg.hpp — me-everywhere #4: cross-session messaging core (pure, header-only).
//
// Peer-to-peer between LIVE sessions (unlike `icmg parallel`'s spawn-and-collect):
// a session sends a directed message (to a session id) or a broadcast ("*"); each
// session reads its inbox — messages addressed to it or broadcast, newer than its
// cursor, excluding its own. Pure serialize/filter; the shared-file append +
// delivery wiring is on top. Mirrors event_bus.hpp.
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace icmg::core {

struct Message {
    int64_t     ts = 0;
    std::string from;     // sender session id
    std::string to;       // recipient session id, or "*" for broadcast
    std::string body;     // free text
};

namespace msg_detail {
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
}  // namespace msg_detail

// One TSV line: ts \t from \t to \t body(escaped).
inline std::string msgToLine(const Message& m) {
    return std::to_string(m.ts) + "\t" + m.from + "\t" + m.to + "\t" + msg_detail::esc(m.body);
}

// Parse back. false on malformed: missing fields, non-numeric ts, empty from/to.
inline bool msgFromLine(const std::string& line, Message& out) {
    size_t t1 = line.find('\t');         if (t1 == std::string::npos) return false;
    size_t t2 = line.find('\t', t1 + 1); if (t2 == std::string::npos) return false;
    size_t t3 = line.find('\t', t2 + 1); if (t3 == std::string::npos) return false;
    try { out.ts = std::stoll(line.substr(0, t1)); } catch (...) { return false; }
    out.from = line.substr(t1 + 1, t2 - t1 - 1);
    out.to   = line.substr(t2 + 1, t3 - t2 - 1);
    out.body = msg_detail::unesc(line.substr(t3 + 1));
    if (out.from.empty() || out.to.empty()) return false;
    return true;
}

// Messages for `me`: addressed to me or broadcast ("*"), strictly newer than
// `since_ts`, excluding my own sends.
inline std::vector<Message> inboxSince(const std::vector<Message>& all,
                                       const std::string& me, int64_t since_ts) {
    std::vector<Message> out;
    for (const auto& m : all) {
        if (m.ts <= since_ts) continue;
        if (m.from == me) continue;
        if (m.to == me || m.to == "*") out.push_back(m);
    }
    return out;
}

}  // namespace icmg::core
