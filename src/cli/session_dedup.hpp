#pragma once
// session_dedup.hpp — TTL-aware per-session injection dedup (Cache-hit optimizer #2).
//
// PROBLEM: `icmg recall` suppressed any memory node already returned this
// session via RefRegistry, but that registry is CALENDAR-DAY scoped — once a
// node is "seen" it stays suppressed for the whole UTC day, across every
// separate conversation. For a long-lived GUI (icemage-code) that over-
// suppresses: a fresh conversation gets memory hidden because an earlier one
// today already surfaced it, and there is no per-conversation reset.
//
// FIX: scope the dedup with a TTL instead of a day bucket — exactly the model
// the file-read dedup already uses (hook_cmd.cpp kReadDedupTTL = 5 min). An id
// injected within the TTL window is suppressed (kills re-injection on the next
// turn, minutes apart); once the window lapses the memory resurfaces (a new
// conversation an hour later is not starved). Append-only flat file, line
// format "<unix_ts>\t<id>"; legacy untimestamped lines match once for compat.
//
// These are free functions taking an explicit file path so they are unit-
// testable without touching the real session file.
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

namespace icmg::cli {

// Default suppression window: long enough to cover an active multi-turn
// conversation (turns are minutes apart), short enough that a later, separate
// conversation re-surfaces the memory. Override via ICMG_RECALL_DEDUP_TTL (sec).
inline int64_t recallDedupTTL() {
    if (const char* e = std::getenv("ICMG_RECALL_DEDUP_TTL")) {
        try { long long v = std::stoll(e); if (v > 0) return (int64_t)v; } catch (...) {}
    }
    return 2700;  // 45 min
}

// True if `id` was injected within `ttl_sec` (entries older are ignored).
inline bool wasInjectedRecently(const std::string& path, const std::string& id,
                                int64_t ttl_sec) {
    std::ifstream f(path);
    if (!f) return false;
    int64_t now = (int64_t)std::time(nullptr);
    std::string line;
    while (std::getline(f, line)) {
        auto tab = line.find('\t');
        if (tab == std::string::npos) {
            // Legacy untimestamped entry — match id, treat as fresh once.
            if (line == id) return true;
            continue;
        }
        int64_t ts = 0;
        try { ts = std::stoll(line.substr(0, tab)); } catch (...) { continue; }
        if (now - ts > ttl_sec) continue;            // expired -> ignore
        if (line.substr(tab + 1) == id) return true;  // live hit
    }
    return false;
}

// Record `id` as injected now (append-only; pruning happens lazily on read via
// the TTL filter above, so the file self-bounds in practice).
inline void markInjected(const std::string& path, const std::string& id) {
    std::ofstream f(path, std::ios::app);
    if (f) f << (int64_t)std::time(nullptr) << "\t" << id << "\n";
}

// v2.21 research A (session-aware recall delta): when dedup suppresses nodes,
// the model must still LEARN they exist -- a suppressed node used to vanish
// with only a stderr note, so the model could not reference or trust prior
// context. Emit ONE compact stdout line carrying the suppressed ids instead of
// their full bodies: the model sees "these earlier facts still apply" for a few
// bytes instead of a re-emission. Returns "" when nothing was suppressed.
inline std::string formatPriorRefLine(const std::vector<std::string>& ids) {
    if (ids.empty()) return "";
    std::string line = "[" + std::to_string(ids.size()) +
                       " prior memor" + (ids.size() == 1 ? "y" : "ies") +
                       " still appl" + (ids.size() == 1 ? "ies" : "y") + ":";
    for (const auto& id : ids) { line += " #"; line += id; }
    line += "]";
    return line;
}

} // namespace icmg::cli
