// src/tkil/history_cost.hpp
// #1 history-cost: analyse the re-send amplification of a session transcript.
//
// An agent re-sends the ENTIRE prior transcript on every turn, so an entry
// recorded early is paid for many times. For a session of N chronological
// entries, the entry at position i (0-based) is included in turns i..N-1, i.e.
// (N - i) times. Its amplified cost = char_len[i] * (N - i). Large EARLY entries
// dominate total spend -- those are the best compaction targets.
//
// Pure + header-only so it is unit-testable without a DB. Token estimate uses
// the common ~4 chars/token heuristic.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace icmg::tkil {

// One chronological transcript entry (oldest first).
struct HistEntry {
    int64_t     id       = 0;
    int64_t     char_len = 0;
    std::string preview;   // optional short label for reporting
};

struct HistCostRow {
    int64_t id            = 0;
    int64_t char_len      = 0;
    int64_t resends       = 0;   // how many turns re-send this entry (N - i)
    int64_t amplified     = 0;   // char_len * resends
    std::string preview;
};

struct HistCostReport {
    int64_t entries          = 0;
    int64_t raw_chars        = 0;   // sum of char_len (sent once each)
    int64_t amplified_chars  = 0;   // sum of char_len*(N-i) = true re-send cost
    double  amplification    = 1.0; // amplified / raw
    std::vector<HistCostRow> hotspots;  // sorted by amplified desc
};

inline int64_t histTokens(int64_t chars) { return chars / 4; }

// Analyse chronological entries (index 0 = oldest). `top` caps the hotspot list.
inline HistCostReport analyzeHistoryCost(const std::vector<HistEntry>& chrono,
                                         size_t top = 10) {
    HistCostReport rep;
    const int64_t n = (int64_t)chrono.size();
    rep.entries = n;
    if (n == 0) return rep;

    std::vector<HistCostRow> rows;
    rows.reserve(chrono.size());
    for (int64_t i = 0; i < n; ++i) {
        HistCostRow r;
        r.id        = chrono[i].id;
        r.char_len  = chrono[i].char_len;
        r.resends   = n - i;                  // entry i re-sent (N - i) times
        r.amplified = chrono[i].char_len * r.resends;
        r.preview   = chrono[i].preview;
        rep.raw_chars       += r.char_len;
        rep.amplified_chars += r.amplified;
        rows.push_back(std::move(r));
    }
    rep.amplification = rep.raw_chars > 0
        ? (double)rep.amplified_chars / (double)rep.raw_chars : 1.0;

    std::sort(rows.begin(), rows.end(),
        [](const HistCostRow& a, const HistCostRow& b) {
            if (a.amplified != b.amplified) return a.amplified > b.amplified;
            return a.id < b.id;
        });
    if (rows.size() > top) rows.resize(top);
    rep.hotspots = std::move(rows);
    return rep;
}

} // namespace icmg::tkil
