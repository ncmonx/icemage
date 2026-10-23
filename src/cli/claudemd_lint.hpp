// v2.26.0: docs-drift helpers for `icmg lint-claudemd`.
//
// Born from arXiv 2610.30571 (Oct 2026): agents follow a stale
// CLAUDE.md/AGENTS.md over the actual code in 90.1% of conflicting cases,
// even when they notice the conflict. Two additions to lint-claudemd:
//   1. Backticked `icmg <sub>` command refs are validated against the live
//      command Registry (previously skipped as generic shell commands).
//   2. When any drift is found, suggest the paper's one-sentence precedence
//      fix, ready to paste into the instruction file.
#pragma once
#include <string>

namespace icmg::cli {

// "icmg recall \"query\"" -> "recall"; "icmg graph symbol Foo" -> "graph";
// non-icmg token or bare "icmg" / flag-first -> "".
inline std::string extractIcmgSubcommand(const std::string& token) {
    const std::string prefix = "icmg ";
    if (token.rfind(prefix, 0) != 0) return "";
    size_t start = prefix.size();
    while (start < token.size() && token[start] == ' ') ++start;
    size_t end = token.find(' ', start);
    std::string sub = (end == std::string::npos)
        ? token.substr(start)
        : token.substr(start, end - start);
    if (sub.empty() || sub[0] == '-') return "";
    return sub;
}

// One-sentence code-precedence guard (arXiv 2610.30571: a single sentence in
// the instruction file cuts stale-file obedience from 90.1% to near zero).
inline std::string precedenceSentence() {
    return "When this file disagrees with the existing code, the existing code "
           "takes precedence: follow the code and update this file.";
}

} // namespace icmg::cli
