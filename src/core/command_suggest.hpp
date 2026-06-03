#pragma once
// Command recommender core: rank known commands against a natural-language intent.
// Pure + model-free -- reuses promptJaccard (word-set similarity) so the long tail
// of rarely-remembered commands becomes discoverable via `icmg suggest "<intent>"`.
// The CLI layer feeds in the live registry (name + description); this header has no
// registry dependency so it is unit-testable in isolation.
#include "prompt_history.hpp"   // promptJaccard
#include "profile_key.hpp"      // slugify (tokenization)
#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace icmg::core {

struct CmdDoc { std::string name, desc; };
struct CmdHit { std::string name; double score = 0.0; };

// Significant tokens of a string (slug split on '-', tokens len>=2 so short command
// names like "ls"/"go" still count).
inline std::set<std::string> cmdSigTokens(const std::string& s, size_t minLen = 2) {
    std::set<std::string> toks;
    const std::string slug = slugify(s);
    size_t pos = 0;
    while (pos <= slug.size()) {
        size_t dash = slug.find('-', pos);
        std::string tok = slug.substr(pos, dash == std::string::npos ? std::string::npos : dash - pos);
        if (tok.size() >= minLen) toks.insert(tok);
        if (dash == std::string::npos) break;
        pos = dash + 1;
    }
    return toks;
}

// Fraction of the command NAME's tokens that appear in the intent. This is the
// high-precision signal: when the user's words contain the command's own name
// (e.g. "compress large output" -> command `compress`), that should dominate the
// weaker description-word overlap.
inline double nameRecall(const std::string& intent, const std::string& name) {
    auto nameToks = cmdSigTokens(name);
    if (nameToks.empty()) return 0.0;
    auto intentToks = cmdSigTokens(intent);
    size_t hit = 0;
    for (const auto& t : nameToks) if (intentToks.count(t)) ++hit;
    return static_cast<double>(hit) / static_cast<double>(nameToks.size());
}

// Score = description-overlap (Jaccard over "name desc") + name-token recall, so a
// command whose name the user actually typed wins over a command that merely shares
// description words. Sort descending; drop zero-score docs; return at most topN.
// Ties keep input order (stable_sort) -> deterministic.
inline std::vector<CmdHit> rankCommands(const std::string& intent,
                                        const std::vector<CmdDoc>& docs, int topN) {
    std::vector<CmdHit> hits;
    hits.reserve(docs.size());
    for (const auto& d : docs) {
        double s = promptJaccard(intent, d.name + " " + d.desc) + nameRecall(intent, d.name);
        if (s > 0.0) hits.push_back({d.name, s});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const CmdHit& a, const CmdHit& b) { return a.score > b.score; });
    if (topN >= 0 && static_cast<int>(hits.size()) > topN) hits.resize(topN);
    return hits;
}

}  // namespace icmg::core
