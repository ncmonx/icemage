#pragma once
// A2 (2026-07-01): pure helpers for `icmg recall --last-session` — surface the
// single most-recent session snapshot + last wflog, with Open items flagged
// ("dimana kamu ketinggalan"). No DB/IO here; recall_cmd.cpp runs the query and
// fills SessionView, so all formatting/classification stays unit-testable.
#include <string>

namespace icmg::cli {

// A memory topic that represents a session handoff/snapshot. These prefixes are
// the reserved ones written by compact-bg (auto-compact-*), the precompact hook
// (session-snapshot*), and manual session logs (session:*).
inline bool isSessionTopic(const std::string& topic) {
    return topic.rfind("session-snapshot", 0) == 0
        || topic.rfind("auto-compact-", 0)    == 0
        || topic.rfind("session:", 0)         == 0
        || topic.rfind("session ", 0)         == 0;
}

// Extract a "Key: value" field from a wflog node's content. wflog stores
// "Goal: ..\nDecisions: ..\nRejected: ..\nOpen: ..". Returns the trimmed value,
// or "" if the key line is absent.
inline std::string wflogField(const std::string& content, const std::string& key) {
    const std::string needle = key + ": ";
    // Match at start-of-content or right after a newline so "Open:" doesn't
    // accidentally hit inside another field's value.
    size_t pos = std::string::npos;
    if (content.rfind(needle, 0) == 0) {
        pos = 0;
    } else {
        size_t at = content.find("\n" + needle);
        if (at != std::string::npos) pos = at + 1;  // skip the '\n'
    }
    if (pos == std::string::npos) return "";
    size_t vstart = pos + needle.size();
    size_t vend = content.find('\n', vstart);
    std::string val = content.substr(vstart,
        vend == std::string::npos ? std::string::npos : vend - vstart);
    // trim trailing whitespace
    while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r'))
        val.pop_back();
    // trim leading whitespace
    size_t b = 0; while (b < val.size() && (val[b] == ' ' || val[b] == '\t')) ++b;
    return val.substr(b);
}

// Data the command gathers from the DB, formatted by renderLastSession.
struct SessionView {
    bool        has_snapshot = false;
    std::string snap_topic, snap_content, snap_age;
    bool        has_wflog = false;
    std::string log_goal, log_decisions, log_open, log_age;
};

// Render the "last session" briefing. Pure: same input -> same output.
inline std::string renderLastSession(const SessionView& v) {
    if (!v.has_snapshot && !v.has_wflog)
        return "No prior session found (no snapshot or workflow log yet).\n";

    std::string s = "=== Last session ===\n";
    if (v.has_snapshot) {
        s += "Snapshot: " + v.snap_topic;
        if (!v.snap_age.empty()) s += "  (" + v.snap_age + ")";
        s += "\n";
        if (!v.snap_content.empty()) {
            // Snapshots store a full session dump; cap so the briefing stays a
            // glance, not a wall. Full content is still reachable via `recall`.
            std::string c = v.snap_content;
            if (c.size() > 240) c = c.substr(0, 240) + " ...";
            s += c + "\n";
        }
    }
    if (v.has_wflog) {
        s += "\nLast workflow log";
        if (!v.log_age.empty()) s += "  (" + v.log_age + ")";
        s += ":\n";
        if (!v.log_goal.empty())      s += "  Goal:      " + v.log_goal + "\n";
        if (!v.log_decisions.empty()) s += "  Decisions: " + v.log_decisions + "\n";
        if (!v.log_open.empty())
            s += "  Open (dimana kamu ketinggalan): " + v.log_open + "\n";
    }
    return s;
}

}  // namespace icmg::cli
