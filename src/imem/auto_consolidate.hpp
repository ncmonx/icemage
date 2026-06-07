// 2026-06-06: auto-consolidate decision helpers (feature #6).
//
// When a memory zone crosses a size threshold and a cooldown has elapsed,
// `icmg memory consolidate --zone X` runs automatically (opt-in) in the
// background. These helpers are the pure decision core; the trigger/wiring
// lives in store_cmd. Cooldown state is a per-zone file marker (epoch seconds).
#pragma once
#include <string>
#include <cctype>

namespace icmg::imem {

// True iff count >= threshold AND cooldown elapsed since last action.
inline bool shouldAutoConsolidate(int zone_count, int threshold,
                                  long long last_ts, long long now_ts,
                                  long long cooldown_s) {
    if (threshold <= 0) return false;
    if (zone_count < threshold) return false;
    return (now_ts - last_ts) >= cooldown_s;
}

// Same gate; used for the rate-limited hint when auto-consolidate is disabled.
inline bool shouldShowHint(int zone_count, int threshold,
                           long long last_ts, long long now_ts,
                           long long cooldown_s) {
    return shouldAutoConsolidate(zone_count, threshold, last_ts, now_ts, cooldown_s);
}

// Filesystem-safe marker filename for a zone (alnum + '_' kept; others -> '_').
inline std::string zoneMarkerName(const std::string& zone) {
    std::string s = "consolidate-";
    for (char c : zone)
        s += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
    s += ".ts";
    return s;
}

// Marker I/O (defined in auto_consolidate.cpp). Stores last-action epoch seconds.
long long readMarkerTs(const std::string& path);   // missing/corrupt -> 0
void      writeMarkerTs(const std::string& path, long long ts);

} // namespace icmg::imem
