#pragma once
// One-shot multi-file intent search. Instead of the model doing a
// Read -> Grep -> Read chain across turns, `icmg find "<intent>"` ranks files by
// relevance to the intent and returns the top files each with only their
// relevant line windows -- the answer in a single turn. Reuses the per-file
// intent slicer (core/intent_slice.hpp). Pure ranking here is unit-testable;
// the filesystem walk lives in the command.
//
// Ranking uses IDF weighting: a rare term (e.g. an exact identifier like
// `modelContextWindow`) outweighs a common one (`model`, `registry`), so the
// file that actually defines/uses the thing wins over keyword-dense noise.
#include "../core/intent_slice.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <utility>
#include <unordered_map>

namespace icmg::cli {

struct FileSlice {
    std::string file;
    std::vector<core::LineRange> ranges;
    double score = 0.0;   // IDF-weighted intent relevance (higher = better)
};

// Count non-overlapping occurrences of needle in haystack.
inline int countOccurrences(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return 0;
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos;
         p = hay.find(needle, p + needle.size())) ++n;
    return n;
}

// Ranking knobs (v2, 2026-11-28) -- defined once. Eval on 18 real queries:
// v1 (raw TF x IDF, substring) scored hit@1 0/18 because huge doc-dense files
// and test files that CALL an identifier many times beat the file defining it.
struct FindRankParams {
    double k1 = 1.2;             // BM25 TF saturation
    double b = 0.75;             // BM25 length normalisation
    double fileNameBonus = 1.5;  // x IDF, term starts a word in the file name
    double dirBonus = 0.5;       // x IDF, term starts a word in a dir segment
    double defBonus = 3.0;       // x IDF, file DEFINES the exact identifier
    size_t defMinLen = 4;        // identifier terms shorter than this get no def bonus
    double testPenalty = 0.75;   // test files, unless the intent mentions tests
};
inline const FindRankParams kFindRank{};

inline bool findIdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// True when orig[pos] starts a word: start of text, after a non-alnum char, or
// a camelCase hump (lower -> Upper). "rank" hits "doRank"/"rank_x", not "frank".
inline bool isWordStart(const std::string& orig, size_t pos) {
    if (pos == 0) return true;
    unsigned char p = (unsigned char)orig[pos - 1], c = (unsigned char)orig[pos];
    if (!std::isalnum(p)) return true;
    return std::islower(p) && std::isupper(c);
}

// Occurrences of `term` (lowercase) in `low` that start a word in `orig`.
inline int countWordStarts(const std::string& low, const std::string& orig,
                           const std::string& term) {
    if (term.empty()) return 0;
    int n = 0;
    for (size_t p = low.find(term); p != std::string::npos;
         p = low.find(term, p + term.size()))
        if (isWordStart(orig, p)) ++n;
    return n;
}

// Does the line around `pos` DEFINE the identifier at [pos, pos+len)?
// Heuristic, language-agnostic: a definition keyword right before it
// (class/struct/def/fn/...), or a signature `<type> name(` whose prefix holds
// no '=', '(' or `return` (those are calls / assignments).
inline bool isDefinitionAt(const std::string& orig, size_t pos, size_t len) {
    size_t ls = orig.rfind('\n', pos == 0 ? 0 : pos - 1);
    ls = (ls == std::string::npos || pos == 0) ? 0 : ls + 1;
    std::string pre = orig.substr(ls, pos - ls);
    // drop trailing qualifiers: ns::Cls::name
    while (pre.size() >= 2 && pre.compare(pre.size() - 2, 2, "::") == 0) {
        pre.resize(pre.size() - 2);
        while (!pre.empty() && findIdentChar(pre.back())) pre.pop_back();
    }
    while (!pre.empty() && (pre.back() == ' ' || pre.back() == '\t')) pre.pop_back();
    if (pre.empty()) return false;
    // Prose guard: a comment line or a name inside a string literal is never a
    // definition ("// sync command (store", "'icmg find(intent)'").
    {
        size_t s = pre.find_first_not_of(" \t");
        std::string lead = s == std::string::npos ? "" : pre.substr(s);
        if (lead.rfind("//", 0) == 0 || lead.rfind("/*", 0) == 0 ||
            lead.rfind("*", 0) == 0 || lead.rfind("--", 0) == 0 ||
            (lead.rfind("#", 0) == 0 && lead.rfind("#define", 0) != 0))
            return false;
        if (pre.find_first_of("'\"`") != std::string::npos) return false;
    }
    static const char* kw[] = {"class", "struct", "enum", "union", "interface",
                               "namespace", "def", "fn", "func", "function", "type",
                               "trait", "impl", "module", "const", "let", "var",
                               "#define", "using", "typedef"};
    for (const char* k : kw) {
        size_t kl = std::strlen(k);
        if (pre.size() >= kl && pre.compare(pre.size() - kl, kl, k) == 0 &&
            (pre.size() == kl || !findIdentChar(pre[pre.size() - kl - 1])))
            return true;
    }
    size_t q = pos + len;
    while (q < orig.size() && (orig[q] == ' ' || orig[q] == '\t')) ++q;
    if (q >= orig.size() || orig[q] != '(') return false;
    if (pre.find('=') != std::string::npos || pre.find('(') != std::string::npos ||
        pre.find("return") != std::string::npos || pre.find('.') != std::string::npos ||
        pre.find("->") != std::string::npos)
        return false;
    char last = pre.back();
    return findIdentChar(last) || last == '*' || last == '&' || last == '>';
}

// File defines `term` as an exact identifier (case-insensitive)?
inline bool definesTerm(const std::string& low, const std::string& orig,
                        const std::string& term) {
    for (size_t p = low.find(term); p != std::string::npos;
         p = low.find(term, p + term.size())) {
        size_t e = p + term.size();
        if ((p > 0 && findIdentChar(orig[p - 1])) || (e < orig.size() && findIdentChar(orig[e])))
            continue;
        if (isDefinitionAt(orig, p, term.size())) return true;
    }
    return false;
}

inline bool isTestPath(const std::string& path) {
    std::string p = path;
    for (auto& c : p) c = c == '\\' ? '/' : (char)std::tolower((unsigned char)c);
    p = "/" + p;
    return p.find("/test/") != std::string::npos || p.find("/tests/") != std::string::npos ||
           p.find("/test_") != std::string::npos || p.find("_test.") != std::string::npos ||
           p.find(".test.") != std::string::npos || p.find(".spec.") != std::string::npos ||
           p.find("/__tests__/") != std::string::npos;
}

// Path bonus for one term: file-name word start > dir word start > none.
inline double pathTermBonus(const std::string& path, const std::string& term,
                            const FindRankParams& P) {
    std::string orig = path;
    for (auto& c : orig) if (c == '\\') c = '/';
    size_t slash = orig.rfind('/');
    std::string dir = slash == std::string::npos ? "" : orig.substr(0, slash);
    std::string file = slash == std::string::npos ? orig : orig.substr(slash + 1);
    auto lowOf = [](std::string s) {
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    if (countWordStarts(lowOf(file), file, term) > 0) return P.fileNameBonus;
    if (countWordStarts(lowOf(dir), dir, term) > 0) return P.dirBonus;
    return 0.0;
}

// Rank (path, body) pairs by intent relevance; return the top maxFiles, each
// with its top windows in reading order. Files with no hit are dropped.
//
// v2 score per file = sum over terms of IDF * (BM25-saturated word-start TF
// + path bonus + definition bonus), scaled by IDF-weighted intent coverage
// and a test-file penalty (lifted when the intent itself mentions tests).
//
// Optional working-set recency boost: when `ageSec` is provided (relpath ->
// file age in seconds), a file's relevance score is multiplied by
//   1 + alpha * exp(-age / halflifeSec)
// so recently-edited files (the active working set) surface first for an
// ambiguous intent. The boost is BOUNDED (<= 1+alpha) and only scales files
// that already match (sc>0) -- it never injects an irrelevant file, and a
// much-more-relevant old file still outranks a barely-relevant fresh one.
inline std::vector<FileSlice> rankFileSlices(
        const std::vector<std::pair<std::string, std::string>>& files,
        const std::string& intent,
        int ctxLines = 4, int maxFiles = 5, int maxWindowsPerFile = 3,
        const std::unordered_map<std::string, double>* ageSec = nullptr,
        double halflifeSec = 86400.0, double alpha = 0.25) {
    auto terms = core::intentTokens(intent);
    std::vector<FileSlice> out;
    if (terms.empty() || files.empty()) return out;

    const FindRankParams& P = kFindRank;
    const int N = (int)files.size();
    const size_t T = terms.size();
    // Lowercased bodies, word-start TF per (file, term), document frequency.
    std::vector<std::string> low(N);
    std::vector<std::vector<int>> tf(N, std::vector<int>(T, 0));
    std::vector<int> df(T, 0);
    double avgLen = 0.0;
    for (int i = 0; i < N; ++i) {
        low[i] = files[i].second;
        for (auto& c : low[i]) c = (char)std::tolower((unsigned char)c);
        avgLen += (double)low[i].size();
        for (size_t t = 0; t < T; ++t) {
            tf[i][t] = countWordStarts(low[i], files[i].second, terms[t]);
            if (tf[i][t] > 0) ++df[t];
        }
    }
    avgLen = std::max(1.0, avgLen / std::max(1, N));
    // IDF weight per term (always positive; rare term -> larger).
    std::vector<double> w(T);
    double wSum = 0.0;
    for (size_t t = 0; t < T; ++t) {
        w[t] = std::log((N + 1.0) / (df[t] + 1.0)) + 1.0;
        wSum += w[t];
    }
    bool intentWantsTests = false;
    for (const auto& t : terms)
        if (t == "test" || t == "tests" || t == "spec") intentWantsTests = true;

    for (int i = 0; i < N; ++i) {
        double sc = 0.0, wHit = 0.0;
        bool bodyHit = false;
        const double lenNorm = 1.0 - P.b + P.b * ((double)low[i].size() / avgLen);
        for (size_t t = 0; t < T; ++t) {
            const double f = (double)tf[i][t];
            // Path bonus counts even when the body lacks the term: the file
            // name is itself evidence (`migrator` -> migrator.cpp).
            double s = pathTermBonus(files[i].first, terms[t], P);
            if (f > 0.0) {
                bodyHit = true;
                s += f * (P.k1 + 1.0) / (f + P.k1 * lenNorm);
                if (terms[t].size() >= P.defMinLen &&
                    definesTerm(low[i], files[i].second, terms[t]))
                    s += P.defBonus;
            }
            if (s <= 0.0) continue;
            sc += s * w[t];
            wHit += w[t];
        }
        if (!bodyHit) continue;   // name-only matches belong to `find --name`
        sc *= 0.5 + 0.5 * (wHit / wSum);   // IDF-weighted intent coverage
        if (!intentWantsTests && isTestPath(files[i].first)) sc *= P.testPenalty;
        auto ranges = core::intentSliceRanges(files[i].second, intent, ctxLines,
                                              maxWindowsPerFile, 60);
        if (ranges.empty()) continue;
        // Working-set recency boost (bounded; only lifts already-matching files).
        if (ageSec) {
            auto it = ageSec->find(files[i].first);
            if (it != ageSec->end() && it->second >= 0.0)
                sc *= 1.0 + alpha * std::exp(-it->second / halflifeSec);
        }
        out.push_back({files[i].first, ranges, sc});
    }
    std::sort(out.begin(), out.end(), [](const FileSlice& a, const FileSlice& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.file < b.file;
    });
    if ((int)out.size() > maxFiles) out.resize(maxFiles);
    return out;
}

}  // namespace icmg::cli
