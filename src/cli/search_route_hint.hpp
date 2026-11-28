#pragma once
// search_route_hint.hpp -- one-line "wrong tool / try this" hints (2026-11-28).
//
// Why: models often loop recall -> recall -> find for one target because
//  (a) `recall` searches MEMORY, but they hand it code identifiers
//      (query_history shows "runner", "run_args", "hasShellOperators"), and
//  (b) a `find` miss prints nothing actionable, so the next try is a guess.
// These pure helpers turn a miss into ONE concrete next command, so the next
// call lands instead of re-guessing. ASCII-only output.
#include <cctype>
#include <string>
#include <vector>

namespace icmg::cli {

// Source-file extensions that mark a query as a file name, not a topic.
inline const std::vector<std::string>& kCodeNameExts() {
    static const std::vector<std::string> v = {
        ".cpp", ".hpp", ".h", ".c", ".cc", ".cs", ".py", ".ts", ".tsx", ".js",
        ".go", ".rs", ".java", ".kt", ".dart", ".rb", ".php", ".swift", ".lua",
        ".ps1", ".sh", ".sql", ".md", ".json", ".toml", ".yaml", ".yml"};
    return v;
}

// Does `q` end in a known source / doc extension?
inline bool hasSourceExt(const std::string& q) {
    std::string low = q;
    for (auto& c : low) c = (char)std::tolower((unsigned char)c);
    for (const auto& e : kCodeNameExts())
        if (low.size() > e.size() && low.compare(low.size() - e.size(), e.size(), e) == 0)
            return true;
    return false;
}

// Does `q` look like a code identifier / file name rather than prose?
// One or two tokens, and at least one has a camelCase hump, an inner '_',
// '::', a path separator, or a source extension.
inline bool looksLikeCodeName(const std::string& q) {
    std::vector<std::string> toks;
    std::string cur;
    for (char c : q) {
        if (std::isspace((unsigned char)c)) { if (!cur.empty()) toks.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) toks.push_back(cur);
    if (toks.empty() || toks.size() > 2) return false;
    for (const auto& t : toks) {
        if (t.find("::") != std::string::npos || t.find('/') != std::string::npos ||
            t.find('\\') != std::string::npos || hasSourceExt(t)) return true;
        size_t u = t.find('_');
        if (u != std::string::npos && u > 0 && u + 1 < t.size()) return true;
        for (size_t i = 1; i < t.size(); ++i)
            if (std::islower((unsigned char)t[i - 1]) && std::isupper((unsigned char)t[i]))
                return true;
    }
    return false;
}

// Hint for `recall <q>` when q is a code name. Empty when not applicable.
inline std::string recallCodeHint(const std::string& q) {
    if (!looksLikeCodeName(q)) return "";
    std::string s = "hint: \"" + q + "\" looks like a code name -- recall searches MEMORY, not code. ";
    if (hasSourceExt(q)) s += "Locate it: icmg find --name " + q + "  |  read it: icmg context <path>";
    else s += "Definition: icmg graph symbol " + q + "  |  usages: icmg grep " + q + " --smart";
    return s + "\n";
}

// Hint for a `find "<intent>"` with no hit: the rarest-looking (longest)
// token as a name/symbol lookup, plus "fewer words" advice for long intents.
inline std::string findMissHint(const std::string& intent) {
    std::vector<std::string> toks;
    std::string cur;
    for (char c : intent) {
        if (std::isalnum((unsigned char)c) || c == '_') cur += c;
        else { if (cur.size() >= 3) toks.push_back(cur); cur.clear(); }
    }
    if (cur.size() >= 3) toks.push_back(cur);
    if (toks.empty()) return "";
    std::string best = toks[0];
    for (const auto& t : toks) if (t.size() > best.size()) best = t;
    std::string s = "hint: try  icmg find --name " + best + "  |  icmg graph symbol " + best +
                    "  |  icmg grep " + best + " --smart";
    if (toks.size() > 3) s += "  |  or fewer, more specific words";
    return s + "\n";
}

// Hint for `find --name <q>` with no hit: the name may live in file CONTENT.
inline std::string nameMissHint(const std::string& q) {
    if (q.empty()) return "";
    return "hint: not a file name? search contents: icmg find \"" + q +
           "\"  |  icmg graph symbol " + q + "\n";
}

}  // namespace icmg::cli
