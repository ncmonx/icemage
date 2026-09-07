// 2026-09-07 token-killer C: dangling-reference guard for salience compression
// (arXiv 2608.04569 "Referential Dangling"). Independent per-line selection
// breaks evidence pairs: the line USING an identifier survives while the line
// DEFINING it is dropped -- the reader meets a name with no referent (34-60%
// of compressed outputs in the paper's benchmark). Deterministic repair pass:
// find entities referenced in kept lines whose defining line (first mention)
// was dropped, and pull those definition lines back. Pure string/set ops.
#pragma once
#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace icmg::core {

// Entity = token likely to carry a referent: CamelCase, ALL_CAPS (>=2 chars),
// snake_case with an underscore, or dotted/scoped identifiers (a.b, a::b).
// Plain lowercase words are ignored (too noisy).
inline bool looksLikeEntity(const std::string& t) {
    if (t.size() < 2) return false;
    bool has_upper = false, has_lower = false, has_us = false, has_scope = false;
    for (size_t i = 0; i < t.size(); ++i) {
        char c = t[i];
        if (std::isupper((unsigned char)c)) has_upper = true;
        else if (std::islower((unsigned char)c)) has_lower = true;
        else if (c == '_') has_us = true;
        else if (c == '.' || c == ':') has_scope = true;
    }
    if (has_upper && has_lower) return true;                    // CamelCase / mixedCase
    if (has_upper && !has_lower && t.size() >= 2) return true;  // ALL_CAPS
    if (has_us && has_lower) return true;                       // snake_case
    if (has_scope && (has_lower || has_upper)) return true;     // a.b / a::b
    return false;
}

// Tokenize a line into candidate entity tokens (alnum + _ . : runs).
inline std::vector<std::string> entityTokens(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '.' || c == ':')
            cur += c;
        else {
            if (looksLikeEntity(cur)) out.push_back(cur);
            cur.clear();
        }
    }
    if (looksLikeEntity(cur)) out.push_back(cur);
    return out;
}

// Given all lines + the keep mask from salience selection, return indices of
// DROPPED lines that must be pulled back: they are the FIRST MENTION of an
// entity that some kept line references. Result is sorted ascending and
// capped (a pathological input must not un-compress everything).
inline std::vector<size_t> danglingRepairLines(const std::vector<std::string>& lines,
                                               const std::vector<bool>& keep,
                                               size_t max_pullback = 16) {
    const size_t n = lines.size();
    if (n == 0 || keep.size() != n) return {};

    // First-mention line per entity.
    std::vector<std::vector<std::string>> toks(n);
    std::set<std::string> seen;
    std::vector<std::pair<std::string, size_t>> first_mention; // entity -> line
    for (size_t i = 0; i < n; ++i) {
        toks[i] = entityTokens(lines[i]);
        for (const auto& t : toks[i])
            if (seen.insert(t).second) first_mention.push_back({t, i});
    }

    // Entities referenced by kept lines AFTER their first mention.
    std::set<size_t> pull;
    for (const auto& [ent, def_line] : first_mention) {
        if (keep[def_line]) continue;              // definition survived
        for (size_t i = def_line + 1; i < n; ++i) {
            if (!keep[i]) continue;
            for (const auto& t : toks[i])
                if (t == ent) { pull.insert(def_line); break; }
            if (pull.count(def_line)) break;
        }
        if (pull.size() >= max_pullback) break;
    }
    return {pull.begin(), pull.end()};
}

} // namespace icmg::core
