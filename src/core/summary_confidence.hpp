#pragma once
// v2.0.0 TE1: confidence score for a generated summary, so a lossy/over-trimmed
// summary is never shipped to the main LLM (hallucination risk). Pure + header-only
// so it is unit-testable with no model. confidence = identifier-retention x
// length-sanity, both in [0,1]. acceptSummary gates on a threshold (default 0.5).
#include <algorithm>
#include <cctype>
#include <set>
#include <string>

namespace icmg::core {

// Lowercased identifier tokens of length >= 3 ([A-Za-z_][A-Za-z0-9_]{2,}).
inline std::set<std::string> summaryIdents(const std::string& s) {
    std::set<std::string> out;
    std::string cur;
    auto flush = [&]() {
        if (cur.size() >= 3) out.insert(cur);
        cur.clear();
    };
    for (char ch : s) {
        unsigned char c = (unsigned char)ch;
        if (std::isalnum(c) || c == '_') cur.push_back((char)std::tolower(c));
        else flush();
    }
    flush();
    return out;
}

// 1.0 for any genuine compression; ramps to 0 only when the "summary" is nearly
// as long as the original (no real compression). Being SHORT is fine — over-trim
// is caught by identifier-coverage, not by a length floor (gists are tiny by
// design and must not be penalised for it).
inline double lengthSanity(size_t origLen, size_t sumLen) {
    if (origLen == 0 || sumLen == 0) return 0.0;
    double r = (double)sumLen / (double)origLen;
    if (r >= 0.98) return 0.0;
    if (r > 0.70) return std::max(0.0, (0.98 - r) / 0.28);  // 0.70..0.98 ramp down
    return 1.0;
}

// [0,1]: how much of the summary can be trusted as a faithful, useful compression.
inline double summaryConfidence(const std::string& original,
                                const std::string& summary) {
    if (original.empty() || summary.empty()) return 0.0;
    auto orig = summaryIdents(original);
    if (orig.empty()) return 0.0;
    auto sum = summaryIdents(summary);
    size_t kept = 0;
    for (const auto& id : orig) if (sum.count(id)) ++kept;
    double coverage = (double)kept / (double)orig.size();
    return coverage * lengthSanity(original.size(), summary.size());
}

// Gate: accept the summary only if confidence meets the threshold; otherwise the
// caller should fall back to the raw/structural slice.
inline bool acceptSummary(const std::string& original,
                          const std::string& summary,
                          double threshold = 0.5) {
    return summaryConfidence(original, summary) >= threshold;
}

}  // namespace icmg::core
