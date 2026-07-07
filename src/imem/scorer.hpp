#pragma once
#include "memory_node.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace icmg::imem {

class Scorer {
public:
    // Singleton — shared across CLI commands in one invocation.
    static Scorer& instance();

    // Build IDF table from corpus. Must call before score/rank.
    void fit(const std::vector<MemoryNode>& corpus);

    // Mark corpus as stale (call on POST_STORE).
    void invalidate() { dirty_ = true; }
    bool isDirty()    const { return dirty_; }

    // v1.29.0 mono-test groundwork: full state reset for tests sharing one
    // process. Clears corpus stats so each TEST() starts from a clean
    // Scorer instance without leaking BM25 weights from prior fit() calls.
    void reset() {
        df_.clear();
        N_ = 0;
        avgdl_ = 0.0;
        dirty_ = true;
    }

    // Full composite score for one node.
    double score(const std::string& query, const MemoryNode& node) const;

    // Breakdown for --explain output.
    struct ScoreDetail {
        double bm25;
        double recency;
        double freq;
        double importance_mult;
        double total;
        std::vector<std::string> matched_tokens;
    };
    ScoreDetail scoreDetailed(const std::string& query, const MemoryNode& node) const;

    // Rank corpus by score, return top limit nodes.
    std::vector<MemoryNode> rank(const std::string& query,
                                  std::vector<MemoryNode> nodes,
                                  int limit = 10) const;

    // v1.23.0: test-visible decay helpers (used by tests/imem/test_importance_decay.cpp).
    // Pure functions with no Scorer state — safe to expose.
    double recencyDecay(int64_t last_used) const;
    double accessAwareDecay(int64_t last_used, int freq) const;
    // v1.21.9 (M2): tier-aware decay — importance 3=critical (frozen),
    // 2=high (half rate), 1=medium (baseline), 0=low (double rate).
    double ageDecay(int64_t created_at, int importance = 1) const;

    // 2026-07-07: pure min-max normalization for hybrid BM25+cosine blending.
    // Root cause: MemoryStore::recallSemantic used a RANK-POSITION fallback
    // (top=1.0, decreasing by index) instead of the real BM25 score
    // magnitude, distorting the blend against cosine similarity. Static +
    // pure (no Scorer state) so it's trivially unit-testable without a
    // fitted corpus. Empty input -> empty. Single value or all-equal ->
    // all 1.0 (no div-by-zero). Otherwise: (x - min) / (max - min).
    static std::vector<double> normalizeMinMax(const std::vector<double>& raw);

    // 2026-07-07: entity-overlap ranking signal (Mem0-style entity linking,
    // deterministic/zero-LLM). icmg already extracts entities ("type:value"
    // tokens: url/ip/env/mention) into candidate.keywords at CAPTURE time
    // (entity_extract.hpp + auto_extract_cmd.cpp) but never cross-references
    // them at RECALL time -- a query mentioning the same URL/env-var/@mention
    // as a memory gets no extra credit for that concrete overlap beyond
    // whatever BM25 already gives the raw text. This closes that gap: exact
    // substring match count of query-entities found in candidate_keywords,
    // normalized to a 0..1 fraction. Empty query_entities -> 0.0 (no signal,
    // caller should skip the boost entirely rather than penalize).
    static double entityOverlapScore(const std::vector<std::string>& query_entities,
                                      const std::string& candidate_keywords);

    // 2026-07-07: fuzzy-match fallback for typo tolerance.
    // Root cause: MemoryStore::recall()/recallUnseen()/recallInZone() all
    // accept a `fuzzy` bool (parsed from CLI `--fuzzy`, help text literally
    // says "Fuzzy search fallback") but it was a dead parameter -- named
    // `bool /*fuzzy*/` (recall/recallInZone) or threaded through unused
    // (recallUnseen) -- never once read. A query with one typo'd token
    // (e.g. "recallSematic") matched NOTHING even with --fuzzy set, silently
    // failing to deliver what the flag promised. This is the pure building
    // block: bounded Levenshtein distance between two lowercase tokens,
    // capped for speed (mirrors icmg::tkil::levenshteinCapped's shape, but
    // implemented locally -- that one is TU-local `static` in dedup_pass.cpp,
    // a different subsystem, not exposed for reuse).
    static int levenshteinCapped(const std::string& a, const std::string& b, int cap);

    // Fraction of query_tokens that have some corpus_token within
    // max_edit_distance (case-sensitive as given -- caller lowercases).
    // Empty query_tokens -> 0.0 (no signal).
    static double fuzzyTokenOverlap(const std::vector<std::string>& query_tokens,
                                     const std::vector<std::string>& corpus_tokens,
                                     int max_edit_distance = 2);

    // 2026-07-07: made public (was private) so MemoryStore::recall()'s fuzzy
    // fallback can tokenize query/corpus text with the SAME normalization
    // BM25 itself uses (lowercase, punctuation-stripped, 2+ chars) -- keeps
    // fuzzy matching consistent with exact matching rather than inventing a
    // second tokenizer. Pure/const, no behavior change for existing callers.
    std::vector<std::string> tokenize(const std::string& text) const;

private:
    Scorer() = default;

    // BM25 parameters
    static constexpr double k1_    = 1.5;
    static constexpr double b_     = 0.75;

    std::unordered_map<std::string, int> df_;   // document frequency per term
    int    N_     = 0;     // corpus size
    double avgdl_ = 0.0;   // avg doc length (tokens)
    bool   dirty_ = true;

    std::string document(const MemoryNode& node) const;
    double bm25(const std::string& query, const MemoryNode& node) const;
    // v1.20.0 (M1) accessAwareDecay + v1.21.9 (M2) ageDecay now public above.
    double idf(const std::string& term) const;
};

} // namespace icmg::imem
