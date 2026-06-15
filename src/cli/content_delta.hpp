// Phase (2026-06-15): line-oriented delta for `icmg context <file> --diff`.
//
// When a file already shown this session is re-requested after an edit, emit
// ONLY the changed lines (with their cur-side line numbers), collapsing runs of
// unchanged lines into "... N unchanged ..." markers. This cuts the token cost
// of an iterative re-read from "whole file" down to "what actually changed".
//
// Membership semantics mirror computePackDelta (pack_delta.hpp): a cur line is
// "changed" when its trimmed text is not present anywhere in the prev body.
// Deterministic + filesystem-free so it is unit-testable directly.
//
// Header-only so tests can include without linking icmg_lib.

#pragma once
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <iomanip>

namespace icmg::cli {

struct ContentDeltaResult {
    std::string text;          // rendered delta (empty when identical)
    int  changed_lines = 0;    // count of cur lines not present in prev
    int  total_lines   = 0;    // total cur lines
    bool identical     = false;// true when nothing changed
};

inline ContentDeltaResult computeContentDelta(const std::string& prev,
                                              const std::string& cur,
                                              int context_lines = 2) {
    auto rtrim = [](std::string s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
        return s;
    };

    // Build the set of prev lines (trimmed) for membership testing.
    std::unordered_set<std::string> prev_set;
    {
        std::istringstream ps(prev);
        std::string ln;
        while (std::getline(ps, ln)) prev_set.insert(rtrim(ln));
    }

    // Split cur into lines (preserving original text for emission).
    std::vector<std::string> lines;
    {
        std::istringstream cs(cur);
        std::string ln;
        while (std::getline(cs, ln)) lines.push_back(rtrim(ln));
    }

    ContentDeltaResult res;
    res.total_lines = (int)lines.size();
    if (context_lines < 0) context_lines = 0;

    // Mark changed lines.
    std::vector<bool> changed(lines.size(), false);
    for (size_t i = 0; i < lines.size(); ++i) {
        // Empty lines are treated as unchanged context to avoid noise.
        if (lines[i].empty()) continue;
        if (!prev_set.count(lines[i])) { changed[i] = true; ++res.changed_lines; }
    }

    if (res.changed_lines == 0) {
        res.identical = true;
        return res;   // empty text
    }

    // Expand visibility window by context_lines around each change.
    std::vector<bool> show(lines.size(), false);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (!changed[i]) continue;
        size_t lo = (i >= (size_t)context_lines) ? i - context_lines : 0;
        size_t hi = i + context_lines;
        if (hi >= lines.size()) hi = lines.size() - 1;
        for (size_t j = lo; j <= hi; ++j) show[j] = true;
    }

    // Render: emit shown lines with 1-based cur line numbers; collapse hidden
    // runs into a single marker.
    std::ostringstream out;
    size_t i = 0;
    while (i < lines.size()) {
        if (show[i]) {
            out << std::setw(6) << (i + 1) << "  " << lines[i] << "\n";
            ++i;
        } else {
            size_t start = i;
            while (i < lines.size() && !show[i]) ++i;
            out << "        ... " << (i - start) << " unchanged line(s) ...\n";
        }
    }
    res.text = out.str();
    return res;
}

} // namespace icmg::cli
