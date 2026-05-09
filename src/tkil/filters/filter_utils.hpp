#pragma once
#include "../base_filter.hpp"
#include <vector>
#include <string>
#include <sstream>

namespace icmg::tkil {

// Phase 67 T27: strip ANSI escape sequences (color, cursor, OSC). Major
// noise source in modern CLIs (npm/cargo/pnpm color output → 30-50% bytes
// are escape codes invisible to the model).
inline std::string stripAnsi(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        // CSI: ESC [ ... letter
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i+1] == '[') {
            i += 2;
            while (i < s.size() && !((s[i] >= '@' && s[i] <= '~'))) ++i;
            continue;
        }
        // OSC: ESC ] ... BEL or ESC backslash terminator
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i+1] == ']') {
            i += 2;
            while (i < s.size() && s[i] != '\x07' && s[i] != '\x1b') ++i;
            if (i < s.size() && s[i] == '\x1b' && i + 1 < s.size() && s[i+1] == '\\') ++i;
            continue;
        }
        // Bare ESC + char (e.g., ESC = ESC >).
        if (s[i] == '\x1b' && i + 1 < s.size()) { ++i; continue; }
        // Carriage-return progress overwrites: collapse `\r` (without \n) as truncation.
        if (s[i] == '\r' && i + 1 < s.size() && s[i+1] != '\n') {
            // Discard everything emitted on this line so far up to last \n.
            auto last_nl = out.rfind('\n');
            if (last_nl == std::string::npos) out.clear();
            else                              out.resize(last_nl + 1);
            continue;
        }
        out += s[i];
    }
    return out;
}

// Phase 67 T27: dedup consecutive identical lines. Build/install commands
// emit huge runs of identical "warning: unused" or "Loading module X 1/200"
// — collapse to "<line>  ×N".
inline std::vector<std::string> dedupConsecutive(const std::vector<std::string>& in) {
    std::vector<std::string> out;
    out.reserve(in.size());
    int run = 1;
    for (size_t i = 0; i < in.size(); ++i) {
        if (i + 1 < in.size() && in[i] == in[i+1] && !in[i].empty()) {
            ++run;
            continue;
        }
        if (run > 1) out.push_back(in[i] + "  ×" + std::to_string(run));
        else         out.push_back(in[i]);
        run = 1;
    }
    return out;
}

// Phase 67 T27: collapse 3+ consecutive blank lines into one blank.
inline std::vector<std::string> collapseBlankRuns(const std::vector<std::string>& in) {
    std::vector<std::string> out;
    out.reserve(in.size());
    int blank_run = 0;
    for (auto& l : in) {
        if (l.empty() || l.find_first_not_of(" \t") == std::string::npos) {
            if (++blank_run > 1) continue;
            out.push_back("");
        } else {
            blank_run = 0;
            out.push_back(l);
        }
    }
    return out;
}

// Split string into lines (with ANSI strip applied first).
inline std::vector<std::string> splitLines(const std::string& s) {
    std::string clean = stripAnsi(s);
    std::vector<std::string> lines;
    std::istringstream ss(clean);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// Apply hard line limit (A9)
inline FilterResult applyHardLimit(FilterResult res) {
    if (res.filtered_lines <= MAX_OUTPUT_LINES) return res;
    auto lines = splitLines(res.output);
    lines.resize(MAX_OUTPUT_LINES);
    std::string out;
    for (auto& l : lines) out += l + "\n";
    int omitted = res.filtered_lines - MAX_OUTPUT_LINES;
    out += "... (output truncated at " + std::to_string(MAX_OUTPUT_LINES)
        + " lines, " + std::to_string(omitted) + " lines omitted) ...\n";
    res.output = out;
    res.was_truncated = true;
    return res;
}

// Check if line contains any of the keywords (case-insensitive)
inline bool containsAny(const std::string& line,
                         const std::vector<std::string>& keywords) {
    std::string low = line;
    for (char& c : low) c = (char)::tolower((unsigned char)c);
    for (auto& kw : keywords) {
        std::string lkw = kw;
        for (char& c : lkw) c = (char)::tolower((unsigned char)c);
        if (low.find(lkw) != std::string::npos) return true;
    }
    return false;
}

} // namespace icmg::tkil
