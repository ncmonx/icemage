// src/tkil/output_delta.hpp
// Delta-only display for `icmg run` — emit only lines that changed vs the
// previous run of the same command. Cuts repeated build/test/lint output by
// 90%+ when nothing changed, or highlights exactly what broke.
//
// Membership semantics mirror content_delta.hpp + pack_delta.hpp:
// a cur line is "changed" when its trimmed text is absent anywhere in prev.
// Order-insensitive — tahan reformat/reorder tanpa false positive.
//
// context_lines = 0 by default (unlike content_delta's 2) because build/test
// error messages are self-contained — surrounding context just adds noise.
//
// Header-only: unit-testable without linking icmg_lib.

#pragma once
#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace icmg::tkil {

// Max bytes of filtered output we persist as snapshot.
// Outputs larger than this use hash-only comparison (no text diff).
static constexpr size_t kDeltaOutputCapBytes = 64 * 1024; // 64 KB

// Keywords that mark "sacred" lines — always emitted even when "identical".
// Evaluated case-insensitively on the trimmed line.
inline bool isSacredLine(const std::string& line) {
    // Quick scan: check lowercase prefix substrings
    std::string lo;
    lo.reserve(line.size());
    for (char c : line) lo += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* kw : {"error", "fatal", "failed", "assert", "panic", "segfault"}) {
        if (lo.find(kw) != std::string::npos) return true;
    }
    return false;
}

struct OutputDeltaResult {
    std::string text;           // delta rendered for user (empty when identical)
    int  added_lines   = 0;     // lines in cur not present in prev
    int  removed_lines = 0;     // lines in prev not present in cur
    int  total_cur     = 0;     // total lines in current output
    bool identical     = false; // true when nothing changed (and no sacred lines)
    bool hash_only     = false; // true when output too large for text diff
    bool first_run     = false; // true when no prev snapshot existed
};

// Compute delta between previous filtered output and current filtered output.
// prev_output: empty string = first run (no snapshot).
// cur_output: filtered output from this run.
// exit_ok: if false (command failed), delta is disabled — returns full output.
// sacred_always: if true, sacred lines (error/fatal/…) always emitted even if identical.
inline OutputDeltaResult computeOutputDelta(
    const std::string& prev_output,
    const std::string& cur_output,
    bool exit_ok       = true,
    bool sacred_always = true)
{
    OutputDeltaResult res;

    // First run: no prev snapshot.
    if (prev_output.empty()) {
        res.first_run = true;
        res.text = cur_output;
        return res;
    }

    // Command failed: always emit full output + disable delta.
    if (!exit_ok) {
        res.text = cur_output;
        return res;
    }

    // Output too large: hash-only path (handled by caller comparing hashes).
    // Here we just note it; caller should check before calling this function
    // if it wants to avoid the string copy. We still handle gracefully.
    if (cur_output.size() > kDeltaOutputCapBytes ||
        prev_output.size() > kDeltaOutputCapBytes) {
        res.hash_only = true;
        // identical flag set by caller via hash comparison
        return res;
    }

    auto rtrim = [](const std::string& s) -> std::string {
        size_t end = s.size();
        while (end > 0 && (s[end-1] == '\r' || s[end-1] == '\n')) --end;
        return s.substr(0, end);
    };

    // Build prev line set.
    std::unordered_set<std::string> prev_set;
    {
        std::istringstream ps(prev_output);
        std::string ln;
        while (std::getline(ps, ln)) prev_set.insert(rtrim(ln));
    }

    // Build cur line set (for removed_lines count).
    std::unordered_set<std::string> cur_set;
    std::vector<std::string> cur_lines;
    {
        std::istringstream cs(cur_output);
        std::string ln;
        while (std::getline(cs, ln)) {
            std::string t = rtrim(ln);
            cur_lines.push_back(t);
            cur_set.insert(t);
        }
    }
    res.total_cur = static_cast<int>(cur_lines.size());

    // Count removed lines (in prev but not cur).
    for (const auto& pl : prev_set) {
        if (!pl.empty() && !cur_set.count(pl)) ++res.removed_lines;
    }

    // Classify cur lines: changed or unchanged.
    std::vector<bool> changed(cur_lines.size(), false);
    for (size_t i = 0; i < cur_lines.size(); ++i) {
        const auto& ln = cur_lines[i];
        if (ln.empty()) continue; // empty = treat as unchanged (avoid noise)
        bool is_new    = (prev_set.find(ln) == prev_set.end());
        bool is_sacred = sacred_always && isSacredLine(ln);
        if (is_new || is_sacred) {
            changed[i] = true;
            if (is_new) ++res.added_lines;
        }
    }

    // All lines unchanged (and no sacred lines forced).
    if (res.added_lines == 0 && res.removed_lines == 0) {
        // Check if any sacred lines exist in cur (they are always emitted).
        bool any_sacred = false;
        if (sacred_always) {
            for (const auto& ln : cur_lines) {
                if (!ln.empty() && isSacredLine(ln)) { any_sacred = true; break; }
            }
        }
        if (!any_sacred) {
            res.identical = true;
            return res; // text stays empty
        }
    }

    // Render changed lines only (context_lines = 0 for command output).
    std::ostringstream out;
    for (size_t i = 0; i < cur_lines.size(); ++i) {
        if (changed[i]) out << cur_lines[i] << "\n";
    }
    res.text = out.str();
    return res;
}

} // namespace icmg::tkil
