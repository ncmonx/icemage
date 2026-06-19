#include "learned_glossary.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>

namespace icmg::compress {

LearnedGlossary::LearnedGlossary(core::Db& db) : db_(db) {
    ensureSchema();
}

void LearnedGlossary::ensureSchema() {
    try {
        db_.run(
            "CREATE TABLE IF NOT EXISTS learned_glossary("
            " phrase    TEXT PRIMARY KEY,"
            " alias     TEXT NOT NULL,"
            " hits      INTEGER NOT NULL DEFAULT 0,"
            " tok_saved INTEGER NOT NULL DEFAULT 0,"
            " last_seen INTEGER NOT NULL DEFAULT 0)");
    } catch (...) { /* best-effort: a read-only/locked db still leaves API usable */ }
}

void LearnedGlossary::recordUse(const std::map<std::string, std::string>& glossary,
                                 int tok_saved_per_entry,
                                 int64_t now) {
    if (now < 0) now = (int64_t)::time(nullptr);
    for (auto& kv : glossary) {
        const std::string& alias  = kv.first;
        const std::string& phrase = kv.second;
        if (phrase.empty()) continue;
        try {
            // Keyed by phrase: bump hits + accumulate savings. The alias is set
            // on first insert and kept stable on conflict (COALESCE keeps old).
            db_.run(
                "INSERT INTO learned_glossary(phrase, alias, hits, tok_saved, last_seen) "
                "VALUES(?,?,1,?,?) "
                "ON CONFLICT(phrase) DO UPDATE SET "
                "  hits      = hits + 1,"
                "  tok_saved = tok_saved + excluded.tok_saved,"
                "  last_seen = excluded.last_seen",
                {phrase, alias, std::to_string(tok_saved_per_entry), std::to_string(now)});
        } catch (...) { /* best-effort */ }
    }
}

int LearnedGlossary::hits(const std::string& phrase) {
    int out = 0;
    try {
        db_.query("SELECT hits FROM learned_glossary WHERE phrase = ?",
                  {phrase},
                  [&](const core::Row& r){ if (!r.empty()) out = std::stoi(r[0]); });
    } catch (...) {}
    return out;
}

std::vector<LearnedEntry> LearnedGlossary::suggestRanked(int min_hits, int limit) {
    std::vector<LearnedEntry> out;
    try {
        db_.query(
            "SELECT phrase, alias, hits, tok_saved, last_seen "
            "FROM learned_glossary WHERE hits >= ? "
            "ORDER BY tok_saved DESC, hits DESC LIMIT ?",
            {std::to_string(min_hits), std::to_string(limit)},
            [&](const core::Row& r){
                if (r.size() < 5) return;
                LearnedEntry e;
                e.phrase    = r[0];
                e.alias     = r[1];
                e.hits      = std::stoll(r[2]);
                e.tok_saved = std::stoll(r[3]);
                e.last_seen = std::stoll(r[4]);
                out.push_back(std::move(e));
            });
    } catch (...) {}
    return out;
}

std::vector<LearnedEntry> LearnedGlossary::suggestRanked(int min_hits, int limit,
                                                         int64_t now,
                                                         double halflife_days) {
    // Pull a generous candidate pool by raw value, then re-rank by recency-
    // decayed score so freshly-used vocab rises and stale entries sink. The
    // table is small; an over-fetch of limit*8 is cheap and keeps the live
    // signal from being capped out by stale-but-high-savings rows.
    int pool = limit > 0 ? limit * 8 : 100;
    auto cands = suggestRanked(min_hits, pool);
    if (now < 0) now = (int64_t)::time(nullptr);
    std::stable_sort(cands.begin(), cands.end(),
        [&](const LearnedEntry& a, const LearnedEntry& b) {
            double va = decayedValue(a.tok_saved, a.last_seen, now, halflife_days);
            double vb = decayedValue(b.tok_saved, b.last_seen, now, halflife_days);
            if (va != vb) return va > vb;
            return a.hits > b.hits;
        });
    if (limit > 0 && (int)cands.size() > limit) cands.resize(limit);
    return cands;
}

std::map<std::string, std::string> LearnedGlossary::suggest(int min_hits, int limit) {
    std::map<std::string, std::string> out;
    // Seed the live compressor with RECENCY-aware ranking (default 30-day
    // half-life) so the vocabulary stays relevant, not just historically large.
    for (auto& e : suggestRanked(min_hits, limit, (int64_t)::time(nullptr), 30.0))
        out[e.alias] = e.phrase;
    return out;
}

int LearnedGlossary::perEntrySaved(int tok_in, int tok_out, int n_entries) {
    if (n_entries <= 0) return 0;
    int saved = tok_in - tok_out;
    if (saved <= 0) return 0;
    return saved / n_entries;
}

double LearnedGlossary::decayedValue(int64_t tok_saved, int64_t last_seen,
                                     int64_t now, double halflife_days) {
    if (halflife_days <= 0.0) return (double)tok_saved;   // decay disabled
    double age_days = (double)(now - last_seen) / 86400.0;
    if (age_days <= 0.0) return (double)tok_saved;        // fresh / future-stamped
    double factor = std::pow(0.5, age_days / halflife_days);
    return (double)tok_saved * factor;
}

int LearnedGlossary::prune(int max_age_days, int min_hits, int64_t now) {
    if (now < 0) now = (int64_t)::time(nullptr);
    int64_t cutoff = now - (int64_t)max_age_days * 86400;
    int removed = 0;
    try {
        // Count first (best-effort metric), then delete dead weight: too old AND
        // not proven (below min_hits). A high-hit phrase is never pruned by age.
        db_.query("SELECT COUNT(*) FROM learned_glossary "
                  "WHERE last_seen < ? AND hits < ?",
                  {std::to_string(cutoff), std::to_string(min_hits)},
                  [&](const core::Row& r){ if (!r.empty()) removed = std::stoi(r[0]); });
        db_.run("DELETE FROM learned_glossary WHERE last_seen < ? AND hits < ?",
                {std::to_string(cutoff), std::to_string(min_hits)});
    } catch (...) { /* best-effort */ }
    return removed;
}

} // namespace icmg::compress
