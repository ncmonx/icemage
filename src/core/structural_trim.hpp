#pragma once
// v2.0.0 C6: structurally-lossless trim for tool output. Drops blank lines, boilerplate
// (pure punctuation / rule-of-dashes), and exact-duplicate lines, preserving order of what
// remains. No semantic loss of distinct content -> safe to apply to noisy command output
// before it enters the window. Pure + deterministic.
#include <cctype>
#include <set>
#include <sstream>
#include <string>

namespace icmg::core {

// A line carries no information if it is blank/whitespace or only punctuation/symbols
// (e.g. "----", "====", "***", "});" alone).
inline bool isBoilerplateLine(const std::string& line) {
    bool sawAlnum = false;
    bool sawNonSpace = false;
    for (char ch : line) {
        unsigned char c = (unsigned char)ch;
        if (!std::isspace(c)) sawNonSpace = true;
        if (std::isalnum(c)) { sawAlnum = true; break; }
    }
    if (!sawNonSpace) return true;   // blank / whitespace-only
    return !sawAlnum;                // non-space but no alphanumerics -> punctuation-only
}

// Trim: keep the first occurrence of each non-boilerplate line, in original order.
inline std::string structuralTrim(const std::string& text) {
    std::istringstream in(text);
    std::set<std::string> seen;
    std::string out, line;
    bool first = true;
    while (std::getline(in, line)) {
        // strip a trailing CR (CRLF inputs)
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (isBoilerplateLine(line)) continue;
        if (!seen.insert(line).second) continue;   // exact duplicate already kept
        if (!first) out += "\n";
        out += line;
        first = false;
    }
    return out;
}

}  // namespace icmg::core
