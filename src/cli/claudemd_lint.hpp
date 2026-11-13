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
#include <cctype>
#include <set>
#include <sstream>
#include <string>
#include <vector>

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

// ── v2.26.1: false-positive cleanup (dogfood run found 38 noise hits) ────────

// Glob patterns, template placeholders, home-relative paths, and markdown
// images are documentation idioms, not real file refs — never lint them.
inline bool isPlaceholderToken(const std::string& tok) {
    if (tok.empty()) return false;
    if (tok[0] == '~' || tok[0] == '!') return true;           // ~/.icmg, ![img](...)
    return tok.find('*') != std::string::npos ||
           tok.find('<') != std::string::npos ||
           tok.find('>') != std::string::npos;
}

// ALL-CAPS identifiers are CMake/preprocessor macros or env vars
// (GLOB_RECURSE, ICMG_WIRE_DIR, FILEVERSION) — they are not graph symbols.
inline bool isMacroOrEnvToken(const std::string& tok) {
    if (tok.size() < 4) return false;
    bool has_alpha = false;
    for (unsigned char c : tok) {
        if (std::islower(c)) return false;
        if (std::isupper(c)) has_alpha = true;
        else if (c != '_' && !std::isdigit(c)) return false;
    }
    return has_alpha;
}

// Known external tools / build targets referenced in build docs. These are
// commands or CMake targets, not source symbols.
inline bool isToolNameToken(const std::string& tok) {
    static const std::set<std::string> kTools = {
        "powershell", "pwsh", "ccache", "lld", "pacman", "ctest", "pytest",
        "icmg_lib", "icmg_test", "onnxruntime", "onnxruntime_providers_shared",
        "msys2", "mingw", "objdump", "gcc", "clang", "msvc",
    };
    return kTools.count(tok) > 0;
}

// snake_case module refs in docs ("batch_builder") usually name a FILE, not a
// symbol. Offer <tok>.hpp / <tok>.cpp basenames to try against the graph.
inline std::vector<std::string> basenameCandidates(const std::string& tok) {
    return { tok + ".hpp", tok + ".cpp", tok + ".h", tok + ".c" };
}

// Per-file opt-out: `<!-- lint-claudemd-ignore: TokA TokB ... -->` anywhere in
// the doc. Returns the set of tokens to skip.
inline std::set<std::string> parseIgnoreList(const std::string& text) {
    std::set<std::string> out;
    const std::string marker = "lint-claudemd-ignore:";
    size_t pos = text.find(marker);
    while (pos != std::string::npos) {
        size_t end = text.find("-->", pos);
        std::string body = (end == std::string::npos)
            ? text.substr(pos + marker.size())
            : text.substr(pos + marker.size(), end - pos - marker.size());
        std::istringstream iss(body);
        std::string w;
        while (iss >> w) out.insert(w);
        pos = text.find(marker, pos + marker.size());
    }
    return out;
}

} // namespace icmg::cli
