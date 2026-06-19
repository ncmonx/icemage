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
    void recordUse(const std::map<std::string, std::string>& glossary,
                   int tok_saved_per_entry = 0);

    // Hit count for a given phrase (0 if unknown).
    int hits(const std::string& phrase);

    // alias -> phrase for every phrase with hits >= min_hits (capped at limit),
    // ordered by accumulated tok_saved desc then hits desc.
    std::map<std::string, std::string> suggest(int min_hits, int limit = 100);

    // Same selection as suggest(), but returns full ranked entries.
    std::vector<LearnedEntry> suggestRanked(int min_hits, int limit = 100);

    // Per-entry token-savings attribution: a compress run saved (tok_in-tok_out)
    // tokens via n_entries glossary aliases; split evenly, clamped at 0 (never
    // credit a compress that grew the text). Pure helper — easy to unit test.
    static int perEntrySaved(int tok_in, int tok_out, int n_entries);

private:
    core::Db& db_;
    void ensureSchema();
};

} // namespace icmg::compress
