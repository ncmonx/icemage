// Phase (2026-06-15): `icmg grep --symbols` — symbol-aware search results.
//
// Raw `rg` output is line-oriented noise: `path:line:text` rows the model must
// mentally map back to "which function is this?". With --symbols, each match is
// grouped under the enclosing function/class (resolved from the graph), so the
// result reads as actionable structure instead of a flat hit list.
//
// Two pure helpers live here so the parse + render logic is unit-testable
// without a DB; the command resolves the enclosing symbol per match in between.
//
// Header-only so tests can include without linking icmg_lib.

#pragma once
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

struct GrepMatch {
    std::string path;
    int         line = 0;
    std::string text;     // matched line content (trimmed of trailing CR/LF)
    std::string symbol;   // enclosing symbol name (empty = top-level)
    std::string kind;     // enclosing symbol kind (function/class/...)
};

// Parse `rg -n` output rows of the form `path:<line>:<text>` into matches.
// Robust to Windows drive-letter paths (the `D:` colon is not a separator) by
// splitting at the first `:<digits>:` occurrence. Lines that don't match the
// pattern (context separators `--`, blanks, "Binary file ... matches") are
// skipped.
inline std::vector<GrepMatch> parseGrepMatches(const std::string& rg_output) {
    std::vector<GrepMatch> out;
    // Path (greedy up to the LAST plausible sep would be wrong; use the FIRST
    // ":<digits>:" — drive colon "D:" is followed by '\' not a digit).
    static const std::regex row_re(R"(^(.+?):(\d+):(.*)$)");
    std::istringstream is(rg_output);
    std::string ln;
    while (std::getline(is, ln)) {
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == '\n')) ln.pop_back();
        if (ln.empty()) continue;
        std::smatch mm;
        if (!std::regex_match(ln, mm, row_re)) continue;
        GrepMatch g;
        g.path = mm[1].str();
        try { g.line = std::stoi(mm[2].str()); } catch (...) { continue; }
        g.text = mm[3].str();
        // Trim leading whitespace from text for compactness.
        size_t s = g.text.find_first_not_of(" \t");
        if (s != std::string::npos) g.text = g.text.substr(s);
        out.push_back(std::move(g));
    }
    return out;
}

// Render matches grouped by file, then by enclosing symbol. Consecutive matches
// sharing a symbol print one header. Matches with no symbol fall under a
// "(no symbol)" bucket so they are never dropped.
inline std::string renderSymbolGrep(const std::vector<GrepMatch>& matches) {
    std::ostringstream out;
    std::string cur_path;
    std::string cur_sym_key;   // symbol|kind, to detect run boundary
    bool first_file = true;
    for (const auto& m : matches) {
        if (m.path != cur_path) {
            if (!first_file) out << "\n";
            first_file = false;
            out << m.path << "\n";
            cur_path = m.path;
            cur_sym_key.clear();   // force a fresh symbol header
        }
        std::string sym_key = m.symbol + "|" + m.kind;
        if (sym_key != cur_sym_key) {
            cur_sym_key = sym_key;
            if (m.symbol.empty())
                out << "  ~ (no symbol)\n";
            else
                out << "  ~ " << (m.kind.empty() ? "" : m.kind + " ") << m.symbol << "\n";
        }
        out << "      " << m.line << ": " << m.text << "\n";
    }
    return out.str();
}

} // namespace icmg::cli
