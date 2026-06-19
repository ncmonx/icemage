#include "learned_glossary.hpp"
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
                                 int tok_saved_per_entry) {
    int64_t now = (int64_t)::time(nullptr);
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

std::map<std::string, std::string> LearnedGlossary::suggest(int min_hits, int limit) {
    std::map<std::string, std::string> out;
    for (auto& e : suggestRanked(min_hits, limit)) out[e.alias] = e.phrase;
    return out;
}

int LearnedGlossary::perEntrySaved(int tok_in, int tok_out, int n_entries) {
    if (n_entries <= 0) return 0;
    int saved = tok_in - tok_out;
    if (saved <= 0) return 0;
    return saved / n_entries;
}

} // namespace icmg::compress
