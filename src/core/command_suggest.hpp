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

// Neighbors of a command = top-N most similar OTHER commands, derived from
// name+desc via rankCommands (zero new data -> the map never rots). If cmdName
// matches a known doc, rank against that doc's "name desc"; otherwise treat
// cmdName as a free-text intent. Self is always excluded.
inline std::vector<CmdHit> neighborsOf(const std::string& cmdName,
                                       const std::vector<CmdDoc>& docs, int n) {
    if (docs.empty() || n <= 0) return {};
    std::string intent = cmdName;
    for (const auto& d : docs)
        if (d.name == cmdName) { intent = d.name + " " + d.desc; break; }
    auto hits = rankCommands(intent, docs, n + 1);   // +1 to absorb self
    std::vector<CmdHit> out;
    for (auto& h : hits) {
        if (h.name == cmdName) continue;             // drop self
        out.push_back(h);
        if (static_cast<int>(out.size()) >= n) break;
    }
    return out;
}

// One-line "related commands" footer for a command's --help (the hallway map at
// decision-time). ASCII-only (Windows console safe; no glyphs). Empty string when
// there are no neighbors so callers can append unconditionally.
inline std::string formatRelatedFooter(const std::string& cmd,
                                       const std::vector<CmdHit>& nb) {
    if (nb.empty()) return std::string();
    std::string s = "\nrelated:";
    for (const auto& h : nb) s += " icmg " + h.name + ",";
    if (!s.empty() && s.back() == ',') s.pop_back();
    s += "   (icmg map " + cmd + ")\n";
    return s;
}

// Pure gate for when to print the related footer (M2 --help + M3 output-run):
//   --help invocation  -> ON by default (suppress with ICMG_NO_MAP_FOOTER).
//   normal run         -> OFF by default; opt in with ICMG_MAP_FOOTER, and only
//                         when the command succeeded (rcOk) to avoid noise on error.
inline bool shouldShowFooter(bool isHelp, bool rcOk,
                             bool noHelpFooterEnv, bool optInRunEnv) {
    if (isHelp) return !noHelpFooterEnv;
    return optInRunEnv && rcOk;
}

// Feature-map M4: a near-duplicate pair = two commands whose "name desc" text
// overlaps at/above `threshold` (symmetric Jaccard). Surfaced by `icmg doctor`
// so an accidental duplicate command is caught at health-check time (durable
// anti-dup, not reliant on the model remembering the reflex rule). Sorted desc.
struct DupPair { std::string a, b; double score = 0.0; };

inline std::vector<DupPair> findNearDuplicateCommands(const std::vector<CmdDoc>& docs,
                                                      double threshold) {
    std::vector<DupPair> out;
    for (size_t i = 0; i < docs.size(); ++i)
        for (size_t j = i + 1; j < docs.size(); ++j) {
            double s = promptJaccard(docs[i].name + " " + docs[i].desc,
                                     docs[j].name + " " + docs[j].desc);
            if (s >= threshold) out.push_back({docs[i].name, docs[j].name, s});
        }
    std::stable_sort(out.begin(), out.end(),
                     [](const DupPair& a, const DupPair& b) { return a.score > b.score; });
    return out;
}

}  // namespace icmg::core
