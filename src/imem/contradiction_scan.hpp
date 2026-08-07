// v2.21 research C: contradiction sentinel.
//
// Bi-temporal invalidation + causal memory-link exist, but nothing ACTIVELY
// looks for pairs of memories that contradict each other -- so the brain can
// hold a stale fact ("X uses A") alongside its correction ("X no longer uses
// A" / "X uses B now") and recall may surface the stale one.
//
// Deterministic, no-LLM heuristic over (id, content, created_at):
//   1. candidate pair = high token overlap (Jaccard >= threshold) -- both talk
//      about the same thing;
//   2. contradiction signal = one side carries a negation/supersede marker
//      ("not", "no longer", "bukan", "deprecated", "->", "instead", ...) OR a
//      shared `key = value` / `key: value` assignment whose values differ;
//   3. direction = older -> newer (the newer one is presumed the correction).
//
// FLAG ONLY -- never deletes. Output feeds `memory invalidate <old>
// --superseded-by <new>` (existing bi-temporal command); a human/agent decides.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::imem {

struct MemFact {
    int64_t     id = 0;
    std::string content;
    int64_t     created_at = 0;
};

struct ContradictionCandidate {
    int64_t     old_id = 0;
    int64_t     new_id = 0;
    std::string reason;
    double      overlap = 0.0;   // Jaccard similarity that paired them
};

namespace contra_detail {

inline std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Word tokens (len >= 3) lowercased; short/noise words dropped.
inline std::set<std::string> tokens(const std::string& text) {
    std::set<std::string> out;
    std::string cur;
    for (char c : text) {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-') cur += (char)std::tolower((unsigned char)c);
        else { if (cur.size() >= 3) out.insert(cur); cur.clear(); }
    }
    if (cur.size() >= 3) out.insert(cur);
    return out;
}

inline double jaccard(const std::set<std::string>& a, const std::set<std::string>& b) {
    if (a.empty() || b.empty()) return 0.0;
    size_t inter = 0;
    for (const auto& t : a) if (b.count(t)) ++inter;
    size_t uni = a.size() + b.size() - inter;
    return uni == 0 ? 0.0 : (double)inter / (double)uni;
}

inline bool hasNegationMarker(const std::string& lc) {
    static const char* kMarkers[] = {
        " not ", "n't ", "no longer", "bukan", "jangan", "tidak lagi",
        "deprecated", "instead of", " instead", "->", "no more",
        "stop using", "removed", "replaced", "supersede", "diganti", "ganti ke",
    };
    for (const char* m : kMarkers)
        if (lc.find(m) != std::string::npos) return true;
    return false;
}

// Extract `key = value` / `key: value` pairs (single-token key and value).
inline std::vector<std::pair<std::string, std::string>> kvPairs(const std::string& lc) {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream in(lc);
    std::string line;
    while (std::getline(in, line)) {
        for (char sep : {'=', ':'}) {
            auto p = line.find(sep);
            if (p == std::string::npos || p == 0 || p + 1 >= line.size()) continue;
            // key = last token before sep; value = first token after.
            auto trim = [](std::string s) {
                size_t b = s.find_first_not_of(" \t");
                size_t e = s.find_last_not_of(" \t");
                return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
            };
            std::string keyPart = trim(line.substr(0, p));
            std::string valPart = trim(line.substr(p + 1));
            auto lastSpace = keyPart.find_last_of(' ');
            std::string key = lastSpace == std::string::npos ? keyPart : keyPart.substr(lastSpace + 1);
            auto firstSpace = valPart.find_first_of(' ');
            std::string val = firstSpace == std::string::npos ? valPart : valPart.substr(0, firstSpace);
            if (!key.empty() && !val.empty() && key.size() >= 2 && val.size() >= 1)
                out.emplace_back(key, val);
        }
    }
    return out;
}

// Same key present on both sides with different values?
inline bool conflictingAssignment(const std::string& lcA, const std::string& lcB,
                                  std::string& whichKey) {
    auto pa = kvPairs(lcA);
    auto pb = kvPairs(lcB);
    for (const auto& [ka, va] : pa)
        for (const auto& [kb, vb] : pb)
            if (ka == kb && va != vb) { whichKey = ka; return true; }
    return false;
}

} // namespace contra_detail

// Scan all pairs; O(n^2) -- callers should pre-filter (e.g. same zone, recent
// window) for large stores. Strongest (highest-overlap) pairs first so a
// result cap keeps the most likely true contradictions. Real-store note: at
// 0.35 threshold a 31k-node store produced ~300k pairs -- default is 0.6.
inline std::vector<ContradictionCandidate>
findContradictionCandidates(const std::vector<MemFact>& facts,
                            double jaccardMin = 0.6) {
    using namespace contra_detail;
    std::vector<ContradictionCandidate> out;

    struct Prep { const MemFact* f; std::string lc; std::set<std::string> toks; };
    std::vector<Prep> prep;
    prep.reserve(facts.size());
    for (const auto& f : facts)
        prep.push_back({&f, lower(f.content), tokens(f.content)});

    for (size_t i = 0; i < prep.size(); ++i) {
        for (size_t j = i + 1; j < prep.size(); ++j) {
            const Prep& A = prep[i];
            const Prep& B = prep[j];
            double sim = jaccard(A.toks, B.toks);
            if (sim < jaccardMin) continue;                 // different topics
            bool negA = hasNegationMarker(A.lc);
            bool negB = hasNegationMarker(B.lc);
            std::string key;
            bool kv = conflictingAssignment(A.lc, B.lc, key);
            // Contradiction = same topic AND (negation on exactly one side, or
            // a conflicting key=value). Both-negated or none-negated with no kv
            // conflict -> likely dup (consolidate's job), skip.
            if (!(negA != negB) && !kv) continue;

            const MemFact* older = A.f;
            const MemFact* newer = B.f;
            if (older->created_at > newer->created_at) std::swap(older, newer);

            std::string reason;
            if (kv)             reason = "conflicting value for '" + key + "'";
            else                reason = "negation marker on one side";
            reason += " (overlap " + std::to_string((int)(sim * 100)) + "%)";

            out.push_back({older->id, newer->id, reason, sim});
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.overlap != b.overlap) return a.overlap > b.overlap;   // strongest first
        return a.old_id != b.old_id ? a.old_id < b.old_id : a.new_id < b.new_id;
    });
    return out;
}

} // namespace icmg::imem
