// Sentinel guard — pure logic for two IO-loop bugs (2026-10-12):
//
//  1. Reaction-ineffectiveness suppression. A WARN check whose auto-reaction
//     does NOT clear the condition (e.g. `backup prune` keeps 15 x 112MB
//     snapshots, so .icmg/ can never fall below disk_mb_max) used to re-fire
//     every scheduled run — a permanent disk-IO loop. RepairCounter never
//     caught it because these reactions don't increment it. Now each check
//     tracks consecutive ineffective reactions in per-project state; after
//     MAX_INEFFECTIVE the reaction is suppressed until the metric recovers
//     (count resets when the check turns OK) or thresholds change.
//
//  2. Cross-session run dedup. Several live sessions (cron + daemon + manual)
//     each ran sentinel within the same minute (audit.log showed 2-3 RUN
//     lines/min). shouldSkipRun() gates a run when the previous one is more
//     recent than min_interval_s. State is per-project, so one project's gate
//     never starves another's.
//
// Pure functions only — no filesystem, no clock: everything injectable for
// tests. sentinel_cmd.cpp owns file placement (.icmg/sentinel-guard.json).
#pragma once

#include <map>
#include <string>
#include <cctype>
#include <cstdint>
#include <sstream>

namespace icmg::cli::sentinel {

// Consecutive ineffective reactions after which a check's reaction is
// suppressed (status still reported WARN; only the auto-reaction stops).
inline constexpr int kMaxIneffective = 2;

// Default minimum seconds between two sentinel runs in the same project.
inline constexpr int kMinRunIntervalS = 300;

struct GuardState {
    long long last_run_ts = 0;
    std::map<std::string, int> ineffective;  // check name -> consecutive count
};

// Tolerant parse of sentinel-guard.json. Unknown keys ignored; missing file
// contents (empty string) give defaults.
inline GuardState parseGuardState(const std::string& json) {
    GuardState st;
    // last_run_ts
    {
        size_t p = json.find("\"last_run_ts\"");
        if (p != std::string::npos) {
            size_t c = json.find(':', p);
            if (c != std::string::npos) {
                size_t s = c + 1;
                while (s < json.size() && std::isspace((unsigned char)json[s])) ++s;
                size_t e = s;
                while (e < json.size() &&
                       (std::isdigit((unsigned char)json[e]) || json[e] == '-')) ++e;
                if (e > s) {
                    try { st.last_run_ts = std::stoll(json.substr(s, e - s)); }
                    catch (...) {}
                }
            }
        }
    }
    // ineffective: {"check":n,...}
    {
        size_t p = json.find("\"ineffective\"");
        if (p == std::string::npos) return st;
        size_t open = json.find('{', p);
        if (open == std::string::npos) return st;
        size_t close = json.find('}', open);
        if (close == std::string::npos) return st;
        std::string body = json.substr(open + 1, close - open - 1);
        size_t pos = 0;
        while (pos < body.size()) {
            size_t q1 = body.find('"', pos);
            if (q1 == std::string::npos) break;
            size_t q2 = body.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            std::string key = body.substr(q1 + 1, q2 - q1 - 1);
            size_t c = body.find(':', q2);
            if (c == std::string::npos) break;
            size_t s = c + 1;
            while (s < body.size() && std::isspace((unsigned char)body[s])) ++s;
            size_t e = s;
            while (e < body.size() && std::isdigit((unsigned char)body[e])) ++e;
            if (e > s) {
                try { st.ineffective[key] = std::stoi(body.substr(s, e - s)); }
                catch (...) {}
            }
            pos = e;
        }
    }
    return st;
}

inline std::string serializeGuardState(const GuardState& st) {
    std::ostringstream o;
    o << "{\"last_run_ts\":" << st.last_run_ts << ",\"ineffective\":{";
    bool first = true;
    for (auto& [k, v] : st.ineffective) {
        if (v <= 0) continue;  // drop healed checks — keeps file minimal
        if (!first) o << ",";
        first = false;
        o << "\"" << k << "\":" << v;
    }
    o << "}}";
    return o.str();
}

// True when this run should be skipped: the previous run in this project is
// more recent than min_interval_s. last_ts==0 (never ran) never skips.
inline bool shouldSkipRun(long long last_ts, long long now, int min_interval_s) {
    if (last_ts <= 0) return false;
    if (now < last_ts) return false;  // clock went backwards — run, don't wedge
    return (now - last_ts) < min_interval_s;
}

// True when the auto-reaction for `check` should be suppressed because the
// last `max_ineffective` reactions did not clear the condition.
inline bool shouldSuppress(const GuardState& st, const std::string& check,
                           int max_ineffective = kMaxIneffective) {
    auto it = st.ineffective.find(check);
    if (it == st.ineffective.end()) return false;
    return it->second >= max_ineffective;
}

// Record a reaction outcome. still_warn=true → the reaction did not clear the
// condition (increment); false → condition cleared (reset). Returns new count.
inline int recordOutcome(GuardState& st, const std::string& check, bool still_warn) {
    int& n = st.ineffective[check];
    n = still_warn ? n + 1 : 0;
    return n;
}

}  // namespace icmg::cli::sentinel
