// src/cli/fuzzy_edit.hpp
// v2.8.1: whitespace-tolerant file edit helper.
//
// Three-level matching cascade (stops at first success):
//   L1 — exact substring match (fast path, same as native Edit)
//   L2 — whitespace-normalised match: strip leading/trailing whitespace
//        per line, compare; apply with the file's own indentation preserved
//   L3 — anchor match: use first non-empty line of old_string to locate
//        the region, then replace by line count
//
// Also handles CRLF vs LF transparently.
//
// Returns FuzzyEditResult with: ok, level, content (new file), diff, hint.

#pragma once

#include <algorithm>
#include <iterator>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

struct FuzzyEditResult {
    bool        ok      = false;
    int         level   = 0;     // 1=exact, 2=ws-norm, 3=anchor; 0=failed
    std::string content;         // new file content (empty if dry_run or failed)
    std::string diff;            // human-readable diff summary
    std::string hint;            // closest-match hint when ok=false
};

namespace detail {

// Strip trailing \r from each line (CRLF -> LF normalisation).
inline std::string stripCR(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (char c : s) if (c != '\r') out.push_back(c);
    return out;
}

// Split string into lines (preserving line endings in each element is NOT
// done here — we work on LF-normalised content).
inline std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) out.push_back(line);
    // preserve trailing newline as empty last element
    if (!s.empty() && s.back() == '\n') out.push_back("");
    return out;
}

// Strip leading + trailing ASCII whitespace from a string.
inline std::string stripWS(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Normalise a multiline string: strip leading/trailing WS per line,
// drop empty lines at start/end, join with '\n'.
inline std::string normLines(const std::string& s) {
    auto lines = splitLines(stripCR(s));
    std::string out;
    for (auto& l : lines) {
        std::string sl = stripWS(l);
        out += sl + "\n";
    }
    // strip trailing newlines
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

// Simple word-set Jaccard similarity for closest-match hint.
inline double jaccard(const std::string& a, const std::string& b) {
    auto words = [](const std::string& s) {
        std::vector<std::string> ws;
        std::string cur;
        for (char c : s) {
            if (std::isalnum((unsigned char)c) || c == '_') cur.push_back(c);
            else { if (!cur.empty()) { ws.push_back(cur); cur.clear(); } }
        }
        if (!cur.empty()) ws.push_back(cur);
        return ws;
    };
    auto wa = words(a), wb = words(b);
    std::sort(wa.begin(), wa.end()); wa.erase(std::unique(wa.begin(), wa.end()), wa.end());
    std::sort(wb.begin(), wb.end()); wb.erase(std::unique(wb.begin(), wb.end()), wb.end());
    std::vector<std::string> inter;
    std::set_intersection(wa.begin(), wa.end(), wb.begin(), wb.end(), std::back_inserter(inter));
    std::vector<std::string> uni;
    std::set_union(wa.begin(), wa.end(), wb.begin(), wb.end(), std::back_inserter(uni));
    return uni.empty() ? 0.0 : (double)inter.size() / uni.size();
}

// Build a minimal unified-style diff summary (not full patch, just context).
inline std::string makeDiff(const std::string& old_str, const std::string& new_str,
                             int level) {
    std::string d = "--- old (level " + std::to_string(level) + " match)\n";
    d += "+++ new\n";
    auto olines = splitLines(stripCR(old_str));
    auto nlines = splitLines(stripCR(new_str));
    size_t n = std::max(olines.size(), nlines.size());
    for (size_t i = 0; i < n; ++i) {
        if (i < olines.size()) d += "- " + olines[i] + "\n";
        if (i < nlines.size()) d += "+ " + nlines[i] + "\n";
    }
    return d;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Main entry point.
// ---------------------------------------------------------------------------
inline FuzzyEditResult fuzzyEdit(
        const std::string& file_content,
        const std::string& old_str,
        const std::string& new_str,
        bool dry_run = false)
{
    FuzzyEditResult res;

    // Normalise line endings in file for matching (keep original for output).
    std::string file_lf  = detail::stripCR(file_content);
    std::string old_lf   = detail::stripCR(old_str);
    std::string new_lf   = detail::stripCR(new_str);

    // --- L1: exact match (LF-normalised) ---
    // Must match at a line boundary: pos==0 or preceded by '\n'.
    // This prevents "  return" matching inside "    return" (4-space contains 2-space substring).
    auto pos = file_lf.find(old_lf);
    while (pos != std::string::npos && pos != 0 && file_lf[pos - 1] != '\n') {
        pos = file_lf.find(old_lf, pos + 1);
    }
    if (pos != std::string::npos) {
        res.ok    = true;
        res.level = 1;
        res.diff  = detail::makeDiff(old_str, new_str, 1);
        if (!dry_run) {
            // Apply on LF-normalised; if original had CRLF preserve it naively
            // by working on the normalised copy (CRLF files are rare in our src).
            res.content = file_lf.substr(0, pos) + new_lf
                        + file_lf.substr(pos + old_lf.size());
        } else {
            res.content = file_content;
        }
        return res;
    }

    // --- L2: whitespace-normalised match ---
    // Normalise both old_str and file content per-line, then find.
    {
        auto file_lines = detail::splitLines(file_lf);
        auto old_lines  = detail::splitLines(old_lf);
        // Remove trailing empty sentinel from splitLines.
        while (!old_lines.empty() && old_lines.back().empty()) old_lines.pop_back();
        if (!old_lines.empty()) {
            size_t n_old = old_lines.size();
            // Build normalised versions for comparison.
            std::vector<std::string> norm_old;
            for (auto& l : old_lines) norm_old.push_back(detail::stripWS(l));
            // Slide window over file lines.
            for (size_t i = 0; i + n_old <= file_lines.size(); ++i) {
                bool match = true;
                for (size_t j = 0; j < n_old; ++j) {
                    if (detail::stripWS(file_lines[i + j]) != norm_old[j]) {
                        match = false; break;
                    }
                }
                if (match) {
                    // Matched at [i .. i+n_old). Build new_lines reusing
                    // file's leading whitespace from matched region.
                    auto new_lines = detail::splitLines(new_lf);
                    while (!new_lines.empty() && new_lines.back().empty())
                        new_lines.pop_back();
                    std::vector<std::string> result_lines = file_lines;
                    // Replace file_lines[i..i+n_old] with new_lines,
                    // adjusting indentation: take leading WS from file line i,
                    // apply to each replacement line (scaled by old indent).
                    std::string base_indent;
                    for (char c : file_lines[i]) {
                        if (c == ' ' || c == '\t') base_indent.push_back(c);
                        else break;
                    }
                    std::vector<std::string> adjusted;
                    for (size_t k = 0; k < new_lines.size(); ++k) {
                        std::string nl = new_lines[k];
                        // If new line had leading WS in old_str, map it to file's indent.
                        std::string old_indent;
                        if (k < old_lines.size()) {
                            for (char c : old_lines[k]) {
                                if (c == ' ' || c == '\t') old_indent.push_back(c);
                                else break;
                            }
                        }
                        std::string new_indent;
                        if (k < new_lines.size()) {
                            for (char c : new_lines[k]) {
                                if (c == ' ' || c == '\t') new_indent.push_back(c);
                                else break;
                            }
                        }
                        // Compute indent delta: new_indent relative to old_indent.
                        // If new has same or less indent than old, use base_indent.
                        // If new has more, add the extra to base_indent.
                        std::string body = detail::stripWS(nl);
                        size_t extra = (new_indent.size() > old_indent.size())
                                        ? (new_indent.size() - old_indent.size()) : 0;
                        std::string indent = base_indent + std::string(extra, ' ');
                        adjusted.push_back(body.empty() ? "" : indent + body);
                    }
                    // Splice into result_lines.
                    result_lines.erase(result_lines.begin() + (int)i,
                                       result_lines.begin() + (int)(i + n_old));
                    result_lines.insert(result_lines.begin() + (int)i,
                                        adjusted.begin(), adjusted.end());
                    res.ok    = true;
                    res.level = 2;
                    res.diff  = detail::makeDiff(old_str, new_str, 2);
                    if (!dry_run) {
                        std::string out;
                        for (size_t ri = 0; ri < result_lines.size(); ++ri) {
                            out += result_lines[ri];
                            if (ri + 1 < result_lines.size()) out += "\n";
                        }
                        // Restore trailing newline if original had one.
                        if (!file_lf.empty() && file_lf.back() == '\n') out += "\n";
                        res.content = out;
                    } else {
                        res.content = file_content;
                    }
                    return res;
                }
            }
        }
    }

    // --- L3: anchor match (first non-empty line of old_string) ---
    {
        auto old_lines = detail::splitLines(old_lf);
        while (!old_lines.empty() && old_lines.back().empty()) old_lines.pop_back();
        std::string anchor;
        for (auto& l : old_lines) {
            anchor = detail::stripWS(l);
            if (!anchor.empty()) break;
        }
        if (!anchor.empty()) {
            auto file_lines = detail::splitLines(file_lf);
            for (size_t i = 0; i < file_lines.size(); ++i) {
                if (detail::stripWS(file_lines[i]) == anchor) {
                    // Found anchor at line i. Replace n_old lines from here.
                    size_t n_old = old_lines.size();
                    if (i + n_old <= file_lines.size()) {
                        auto new_lines = detail::splitLines(new_lf);
                        while (!new_lines.empty() && new_lines.back().empty())
                            new_lines.pop_back();
                        std::string base_indent;
                        for (char c : file_lines[i]) {
                            if (c == ' ' || c == '\t') base_indent.push_back(c);
                            else break;
                        }
                        std::vector<std::string> result_lines = file_lines;
                        std::vector<std::string> adjusted;
                        for (auto& nl : new_lines) {
                            std::string body = detail::stripWS(nl);
                            adjusted.push_back(body.empty() ? "" : base_indent + body);
                        }
                        result_lines.erase(result_lines.begin() + (int)i,
                                           result_lines.begin() + (int)(i + n_old));
                        result_lines.insert(result_lines.begin() + (int)i,
                                            adjusted.begin(), adjusted.end());
                        res.ok    = true;
                        res.level = 3;
                        res.diff  = detail::makeDiff(old_str, new_str, 3);
                        if (!dry_run) {
                            std::string out;
                            for (size_t ri = 0; ri < result_lines.size(); ++ri) {
                                out += result_lines[ri];
                                if (ri + 1 < result_lines.size()) out += "\n";
                            }
                            if (!file_lf.empty() && file_lf.back() == '\n') out += "\n";
                            res.content = out;
                        } else {
                            res.content = file_content;
                        }
                        return res;
                    }
                }
            }
        }
    }

    // --- No match: build hint ---
    {
        auto file_lines = detail::splitLines(file_lf);
        std::string anchor;
        for (auto& l : detail::splitLines(old_lf)) {
            anchor = detail::stripWS(l);
            if (!anchor.empty()) break;
        }
        double best = -1.0; size_t best_i = 0;
        for (size_t i = 0; i < file_lines.size(); ++i) {
            double sc = detail::jaccard(anchor, file_lines[i]);
            if (sc > best) { best = sc; best_i = i; }
        }
        res.hint = "old_string not found (tried exact, ws-normalised, anchor).\n";
        res.hint += "Anchor: " + anchor + "\n";
        if (!file_lines.empty()) {
            res.hint += "Closest line (" + std::to_string((int)(best * 100))
                      + "% similarity) L" + std::to_string(best_i + 1) + ": "
                      + file_lines[best_i] + "\n";
            res.hint += "Tip: re-read with `icmg context <file> --lines "
                      + std::to_string(best_i > 3 ? best_i - 3 : 1) + "-"
                      + std::to_string(best_i + 5) + "` then fix old_string.";
        }
    }
    return res;
}

} // namespace icmg::cli
