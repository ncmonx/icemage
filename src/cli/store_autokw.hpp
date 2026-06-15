#pragma once
// Auto-keyword derivation for `icmg store` (semantic-title v2).
// When the user stores a memory without --kw, derive a compact, salient keyword
// set from the content so recall/BM25 has signal even on terse captures.
//
// Pure + DB-free (header-only) so the extraction is unit-tested in isolation,
// same pattern as recall_index.hpp / private_filter.hpp. Heuristic, not ML:
//   - tokenize on non-alnum (keep intra-word letters/digits)
//   - lowercase, drop a small English/Indonesian stopword set
//   - drop very short tokens (< 3 chars) and pure numbers
//   - dedup (first occurrence wins, preserving salience order)
//   - cap to maxKw, comma-join

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace icmg::cli {

inline bool isStopword(const std::string& w) {
    static const std::vector<std::string> stop = {
        // English
        "the","a","an","of","and","to","in","is","it","on","at","for","was",
        "were","be","been","with","as","by","or","that","this","from","but",
        "are","has","had","have","not","no","so","if","then","than","we","you",
        "i","he","she","they","its","our","your","their","will","can","because",
        // Indonesian (common)
        "yang","dan","di","ke","dari","untuk","ini","itu","ada","pada","dengan",
        "saya","kamu","aku","tidak","sudah","akan","atau","juga","karena",
    };
    return std::find(stop.begin(), stop.end(), w) != stop.end();
}

// Derive up to maxKw comma-separated keywords from content. First-seen order is
// preserved so the leading (usually most-topical) terms win.
inline std::string autoKeywords(const std::string& content, size_t maxKw = 6) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&](){
        if (cur.empty()) return;
        std::string w = cur; cur.clear();
        // lowercase
        std::transform(w.begin(), w.end(), w.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        if (w.size() < 3) return;                 // too short
        bool allDigit = std::all_of(w.begin(), w.end(),
                                    [](unsigned char c){ return std::isdigit(c); });
        if (allDigit) return;                     // pure number
        if (isStopword(w)) return;
        if (std::find(out.begin(), out.end(), w) != out.end()) return; // dedup
        out.push_back(std::move(w));
    };
    for (char c : content) {
        if (std::isalnum((unsigned char)c)) cur.push_back(c);
        else flush();
    }
    flush();
    if ((size_t)out.size() > maxKw) out.resize(maxKw);
    std::string res;
    for (size_t i = 0; i < out.size(); ++i) {
        if (i) res += ",";
        res += out[i];
    }
    return res;
}

} // namespace icmg::cli
