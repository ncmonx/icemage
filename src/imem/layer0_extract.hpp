// 2026-06-07: Layer-0 rule-based auto-extract (luna idea, STANDOUT: zero-LLM memory capture
// from tool events). Pure classifier — no DB/IO. A PostToolUse hook feeds (cmd, output, exit);
// this decides whether to persist and as what. Cheap: pure string rules, no model call.
#pragma once
#include <string>
#include <vector>
#include <algorithm>

namespace icmg::imem {

struct Layer0Result {
    std::string kind;     // "skip" | "wflog" | "known-issue"
    std::string content;  // the line to persist (empty when skip)
};

// First non-empty line of s, trimmed of trailing CR, capped to 200 chars.
inline std::string firstMeaningfulLine(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        size_t e = s.find('\n', i);
        std::string line = s.substr(i, e == std::string::npos ? std::string::npos : e - i);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        size_t b = line.find_first_not_of(" \t");
        if (b != std::string::npos) line = line.substr(b);
        if (!line.empty()) return line.size() > 200 ? line.substr(0, 200) : line;
        if (e == std::string::npos) break;
        i = e + 1;
    }
    return "";
}

inline bool containsCI(const std::string& hay, const std::string& needle) {
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
        [](char a, char b){ return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
    return it != hay.end();
}

// Classify a tool event into a zero-LLM memory action.
// Rules (unambiguous, high-signal only):
//  1. successful `git commit`  -> wflog (record the commit subject)
//  2. non-zero exit WITH an error marker in output -> known-issue candidate
//  3. everything else -> skip (no noise)
inline Layer0Result classifyToolEvent(const std::string& cmd, const std::string& output, int exitCode) {
    if (containsCI(cmd, "git commit") && exitCode == 0) {
        std::string line = firstMeaningfulLine(output);
        return { "wflog", line.empty() ? "git commit" : ("commit: " + line) };
    }
    if (exitCode != 0) {
        static const char* kErr[] = { "error:", "fatal:", "FAILED", "Traceback",
            "exception", "undefined reference", "LNK", "cannot open", "Segmentation fault" };
        for (auto* m : kErr) {
            if (containsCI(output, m)) {
                std::string line = firstMeaningfulLine(output);
                if (!line.empty()) return { "known-issue", line };
            }
        }
    }
    return { "skip", "" };
}

} // namespace icmg::imem
