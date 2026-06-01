// v1.56 T1 Stage 2: dedup pass.
//
// Collapses adjacent identical / near-identical lines into "<line> (×N)"
// once a run reaches `min_run` length (default 3). Error/fatal lines on
// the allowlist are NEVER collapsed (regression catch).
//
// Streaming, O(n). One-line lookback buffer.

#pragma once

#include <string>
#include <cstddef>

namespace icmg::tkil {

struct DedupOpts {
    // Minimum run length to collapse. 3 is conservative — 2 of the same line
    // often appears in real output without being noise (e.g. two warnings).
    int    min_run      = 3;

    // Shared-prefix ratio for near-identical matching. 1.0 = require exact
    // match. 0.8 = lines sharing ≥80% of the first N characters collapse.
    double prefix_ratio = 0.8;

    // Levenshtein distance cap for near-identical matching. <0 disables.
    // 0 = require exact match. 3 = allow up to 3 char edits.
    int    max_levenshtein = -1;   // off by default (prefix_ratio is cheaper)

    // Marker rendered after the collapsed line. {N} is replaced by run count.
    // Default uses the Unicode "×" multiplication sign.
    std::string marker_format = " (\xc3\x97{N})";
};

// Apply dedup pass to a text blob. Splits on '\n' (LF or CRLF accepted),
// emits LF-terminated lines (or matches input trailing-newline behaviour).
std::string dedupPass(const std::string& in, const DedupOpts& opts = {});

// Exposed for testing: returns true if `line` matches the always-verbatim
// allowlist (error / fail / FATAL / LNK\d{4} / RC\d{4} / C\d{4}: etc.).
bool isAlwaysVerbatim(const std::string& line);

} // namespace icmg::tkil
