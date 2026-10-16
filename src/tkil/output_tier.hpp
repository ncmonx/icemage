// src/tkil/output_tier.hpp
// Auto-tier output classifier for `icmg run` — classify command output by
// urgency so each tier gets the right verbosity. Composes ABOVE delta-only:
// classify() always consumes FULL output; delta is a sub-renderer of the
// SUCCESS tier only. This guarantees a warning keyword in an unchanged line
// still bumps to WARNING (delta can never demote it to SUCCESS).
//
// Tiers:
//   ERROR   — exit_code != 0, OR a sacred keyword (error/fatal/FAILED/...) present
//             → full output
//   WARNING — a warn keyword (warning/warn/deprecated) present
//             → first N lines + count summary (delta disabled)
//   SUCCESS — neither → delta sub-render ('.', '~ ok (N/M)', or full first-run)
//
// Header-only: unit-testable without linking icmg_lib. Pure (no I/O).

#pragma once
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::tkil {

enum class OutputTier { Error, Warning, Success };

// Lowercase a string (ASCII).
inline std::string tierLower(const std::string& s) {
    std::string lo;
    lo.reserve(s.size());
    for (char c : s) lo += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lo;
}

// Does the line contain an ERROR-class keyword?
inline bool tierHasErrorKeyword(const std::string& lower_line) {
    for (const char* kw : {"error", "fatal", "failed", "panic", "segfault",
                           "abort", "assertion"}) {
        if (lower_line.find(kw) != std::string::npos) return true;
    }
    return false;
}

// Does the line contain a WARNING-class keyword?
inline bool tierHasWarnKeyword(const std::string& lower_line) {
    for (const char* kw : {"warning", "warn:", " warn ", "deprecated"}) {
        if (lower_line.find(kw) != std::string::npos) return true;
    }
    return false;
}

// Git notices that say "warning" but carry no information about the command's
// result. On Windows nearly every `git diff` / `git status` / `git add` prints
// one per touched file ("LF will be replaced by CRLF"), plus "unable to find
// all commit-graph files" on a stale graph. They tripped the WARNING tier and
// the 3-line head summary then showed ONLY these notices and cut the real
// output -- the command had to be re-run (measured twice in one session,
// 2026-10-15). Matched as whole-notice shapes, never on a bare keyword.
inline bool tierIsBenignNotice(const std::string& line) {
    if (line.rfind("warning: ", 0) != 0) return false;
    const bool crlf = line.find("in the working copy of '") != std::string::npos &&
                      (line.find("LF will be replaced by CRLF") != std::string::npos ||
                       line.find("CRLF will be replaced by LF") != std::string::npos);
    const bool cgraph = line.find("unable to find all commit-graph files") != std::string::npos;
    return crlf || cgraph;
}

// Drop benign notices before classification; `hidden` gets the count so the
// caller can say they were removed (never silently).
inline std::string stripBenignNotices(const std::string& out, int* hidden = nullptr) {
    std::string kept;
    kept.reserve(out.size());
    int n = 0;
    std::size_t pos = 0;
    while (pos < out.size()) {
        std::size_t nl = out.find('\n', pos);
        const std::size_t end = nl == std::string::npos ? out.size() : nl + 1;
        std::string line = out.substr(pos, end - pos);
        std::string bare = line;
        while (!bare.empty() && (bare.back() == '\n' || bare.back() == '\r')) bare.pop_back();
        if (tierIsBenignNotice(bare)) ++n;
        else kept += line;
        pos = end;
    }
    if (hidden) *hidden = n;
    return kept;
}

// Pure classifier: exit_code + full output → tier.
inline OutputTier classifyTier(int exit_code, const std::string& full_output) {
    if (exit_code != 0) return OutputTier::Error;

    bool any_warn = false;
    std::istringstream is(full_output);
    std::string line;
    while (std::getline(is, line)) {
        if (tierIsBenignNotice(line)) continue;
        std::string lo = tierLower(line);
        if (tierHasErrorKeyword(lo)) return OutputTier::Error; // error wins immediately
        if (tierHasWarnKeyword(lo))  any_warn = true;
    }
    return any_warn ? OutputTier::Warning : OutputTier::Success;
}

struct TierSummary {
    OutputTier  tier;
    std::string text;        // rendered summary for WARNING tier (empty otherwise)
    int         total_lines; // total lines in full output
    int         shown_lines; // lines shown in summary
};

// Render a WARNING-tier summary: first N lines + a "(… N more lines)" tail.
inline TierSummary renderWarningSummary(const std::string& full_output,
                                        int head_lines = 3) {
    TierSummary s;
    s.tier = OutputTier::Warning;
    std::vector<std::string> lines;
    std::istringstream is(full_output);
    std::string ln;
    while (std::getline(is, ln)) lines.push_back(ln);
    s.total_lines = static_cast<int>(lines.size());
    if (head_lines < 0) head_lines = 0;

    std::ostringstream out;
    int shown = 0;
    for (int i = 0; i < (int)lines.size() && i < head_lines; ++i) {
        out << lines[i] << "\n";
        ++shown;
    }
    s.shown_lines = shown;
    int remaining = s.total_lines - shown;
    if (remaining > 0) {
        out << "[... " << remaining << " more line(s); --no-tier for full]\n";
    }
    s.text = out.str();
    return s;
}

} // namespace icmg::tkil
