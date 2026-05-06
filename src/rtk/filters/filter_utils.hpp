#pragma once
#include "../base_filter.hpp"
#include <vector>
#include <string>
#include <sstream>

namespace icmg::rtk {

// Split string into lines
inline std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        // Strip trailing \r
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

} // namespace icmg::rtk
