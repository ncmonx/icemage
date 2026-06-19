// Slice-1 of Adaptive Output Gate: LearnedGlossary.
//
// A cross-session vocabulary of compression aliases. Every time `icmg compress`
// produces a glossary, the aliases that actually map real phrases are recorded
// here with a hit count + accumulated token savings. Over time the compressor
// can SEED itself with the highest-value, most-recurring mappings instead of
// rediscovering them per call -> self-improving compression.
//
// Schema is created on-demand (CREATE TABLE IF NOT EXISTS) so no migration file
// is touched; mirrors profile_store / chat_persistence.
#pragma once
#include "../core/db.hpp"
#include <map>
#include <string>
#include <vector>

namespace icmg::compress {

struct LearnedEntry {
    std::string alias;
    std::string phrase;
    int64_t     hits      = 0;
    int64_t     tok_saved = 0;
    int64_t     last_seen = 0;
};

class LearnedGlossary {
public:
    explicit LearnedGlossary(core::Db& db);

    // Record a produced glossary (alias -> phrase). Each entry bumps hits by 1
    // and adds tok_saved_per_entry to the phrase's accumulated savings.
    // Keyed by phrase (the original text); the first alias seen is kept stable.
    // `now` overridable for deterministic tests (-1 -> wall clock).
    void recordUse(const std::map<std::string, std::string>& glossary,
                   int tok_saved_per_entry = 0,
                   int64_t now = -1);

    // Hit count for a given phrase (0 if unknown).
    int hits(const std::string& phrase);

    // alias -> phrase for every phrase with hits >= min_hits (capped at limit),
    // ordered by accumulated tok_saved desc then hits desc.
    std::map<std::string, std::string> suggest(int min_hits, int limit = 100);

    // Same selection as suggest(), but returns full ranked entries.
    std::vector<LearnedEntry> suggestRanked(int min_hits, int limit = 100);

    // Recency-aware ranking (Slice-4 durability): candidates are re-ranked by
    // decayedValue(now) so a freshly-used phrase outranks a stale high-savings
    // one — keeps the seeded vocabulary RELEVANT over time, not just large.
    std::vector<LearnedEntry> suggestRanked(int min_hits, int limit,
                                            int64_t now, double halflife_days);

    // Per-entry token-savings attribution: a compress run saved (tok_in-tok_out)
    // tokens via n_entries glossary aliases; split evenly, clamped at 0 (never
    // credit a compress that grew the text). Pure helper — easy to unit test.
    static int perEntrySaved(int tok_in, int tok_out, int n_entries);

    // Recency-decayed value of a phrase: tok_saved discounted by age (from
    // last_seen) via an exponential half-life. halflife_days <= 0 disables
    // decay (returns tok_saved). Pure/static — deterministic to unit-test.
    static double decayedValue(int64_t tok_saved, int64_t last_seen,
                               int64_t now, double halflife_days);

    // Forget dead weight: delete entries not seen for > max_age_days AND below
    // min_hits. Keeps the vocabulary curated so stale noise can't crowd out live
    // signal. Returns rows removed. `now` overridable for tests (-1 -> clock).
    int prune(int max_age_days, int min_hits, int64_t now = -1);

private:
    core::Db& db_;
    void ensureSchema();
};

} // namespace icmg::compress
