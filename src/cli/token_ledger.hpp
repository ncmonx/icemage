#pragma once
// token_ledger.hpp — REAL Anthropic API token ledger (Gap #3).
//
// Every API response carries a `usage` block (input / output / cache-read /
// cache-creation tokens). The GUI (icemage-code AgentLoop) records one row per
// turn through `icmg token-ledger record`; `icmg savings` reads it for an
// honest meter instead of the filter/denial proxy estimate.
//
// These free functions isolate the DB write/read so they are unit-testable
// without going through argv (mirrors savings_daily.hpp). The ensure-table
// helper guarded-creates the schema so hand-built test fixtures + DBs that
// skipped the migrator still work.
#include "../core/db.hpp"
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace icmg::cli {

struct TokenLedgerEntry {
    std::string session_id;
    std::string model;
    std::string source = "gui";
    int64_t input_tokens = 0;
    int64_t output_tokens = 0;
    int64_t cache_read_tokens = 0;
    int64_t cache_creation_tokens = 0;
};

struct TokenLedgerTotals {
    int64_t rows = 0;
    int64_t input = 0;
    int64_t output = 0;
    int64_t cache_read = 0;
    int64_t cache_creation = 0;
    // Billed input = fresh input + cache-write (cache reads are ~10% priced but
    // we surface them separately; callers decide). Convenience sum:
    int64_t totalInput() const { return input + cache_read + cache_creation; }
    // Cache-hit rate: fraction of the input context served from cache (read).
    // KV-cache hits are billed at ~10% of fresh input, so a higher rate = a
    // cheaper, more cache-friendly prompt. 0 when there is no input at all
    // (guards divide-by-zero). Range 0..1.
    double cacheHitRate() const {
        int64_t denom = totalInput();
        if (denom <= 0) return 0.0;
        return (double)cache_read / (double)denom;
    }
};

// Guarded CREATE so fixtures / un-migrated DBs gain the table. Idempotent.
inline void ensureTokenLedger(core::Db& db) {
    db.run("CREATE TABLE IF NOT EXISTS token_ledger ("
           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " ts INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
           " session_id TEXT NOT NULL DEFAULT '',"
           " model TEXT NOT NULL DEFAULT '',"
           " source TEXT NOT NULL DEFAULT 'gui',"
           " input_tokens INTEGER NOT NULL DEFAULT 0,"
           " output_tokens INTEGER NOT NULL DEFAULT 0,"
           " cache_read_tokens INTEGER NOT NULL DEFAULT 0,"
           " cache_creation_tokens INTEGER NOT NULL DEFAULT 0)");
}

// Insert one usage row. Skips a fully-zero entry (no real usage to record).
inline bool recordTokenLedger(core::Db& db, const TokenLedgerEntry& e) {
    if (e.input_tokens <= 0 && e.output_tokens <= 0 &&
        e.cache_read_tokens <= 0 && e.cache_creation_tokens <= 0)
        return false;
    ensureTokenLedger(db);
    db.run("INSERT INTO token_ledger"
           " (session_id, model, source, input_tokens, output_tokens,"
           "  cache_read_tokens, cache_creation_tokens)"
           " VALUES (?, ?, ?, ?, ?, ?, ?)",
           {e.session_id, e.model, e.source,
            std::to_string(e.input_tokens), std::to_string(e.output_tokens),
            std::to_string(e.cache_read_tokens),
            std::to_string(e.cache_creation_tokens)});
    return true;
}

// Aggregate the ledger within the last `window_days` (<=0 = all time).
inline TokenLedgerTotals aggregateTokenLedger(core::Db& db, int window_days) {
    TokenLedgerTotals t;
    ensureTokenLedger(db);
    std::string sql =
        "SELECT COUNT(*), COALESCE(SUM(input_tokens),0),"
        " COALESCE(SUM(output_tokens),0), COALESCE(SUM(cache_read_tokens),0),"
        " COALESCE(SUM(cache_creation_tokens),0) FROM token_ledger";
    std::vector<std::string> params;
    if (window_days > 0) {
        int64_t cutoff = (int64_t)std::time(nullptr) - (int64_t)window_days * 86400;
        sql += " WHERE ts > ?";
        params.push_back(std::to_string(cutoff));
    }
    db.query(sql, params, [&](const core::Row& r) {
        if (r.size() < 5) return;
        try {
            t.rows           = std::stoll(r[0]);
            t.input          = std::stoll(r[1]);
            t.output         = std::stoll(r[2]);
            t.cache_read     = std::stoll(r[3]);
            t.cache_creation = std::stoll(r[4]);
        } catch (...) {}
    });
    return t;
}

// Bug report 2026-07-10 (icmg-savings-daily-history.md): the "Daily
// real-token history" block in `icmg savings` used to aggregate a DIFFERENT
// source (context-budget --all-sessions transcript-file mtime + text-length
// estimate) than the "Real API tokens" headline (this token_ledger table).
// Two disagreeing numbers in one command's output -- missing days (a still-
// growing transcript file only ever counted on its LAST mtime, so a busy
// multi-day session could vanish from the earlier days) and wrong magnitude
// (session-token estimate vs real API-billed tokens are different scales).
//
// This is the fix: bucket token_ledger itself by local calendar day, so the
// daily list is always the SAME source of truth as the headline, and every
// row (turn) counts on the day it actually happened.
struct DailyTokenRow {
    std::string day;     // YYYY-MM-DD, local time
    int64_t tokens = 0;  // input+output+cache_read+cache_creation for that day
    int64_t turns = 0;   // row count that day
};

// Newest-first, capped at max_rows (<=0 = unlimited).
inline std::vector<DailyTokenRow> aggregateTokenLedgerByDay(core::Db& db, int max_rows) {
    std::vector<DailyTokenRow> out;
    ensureTokenLedger(db);
    std::string sql =
        "SELECT date(ts,'unixepoch','localtime') AS day,"
        " COALESCE(SUM(input_tokens+output_tokens+cache_read_tokens+cache_creation_tokens),0),"
        " COUNT(*) FROM token_ledger GROUP BY day ORDER BY day DESC";
    if (max_rows > 0) sql += " LIMIT " + std::to_string(max_rows);
    db.query(sql, {}, [&](const core::Row& r) {
        if (r.size() < 3) return;
        DailyTokenRow row;
        row.day = r[0];
        try {
            row.tokens = std::stoll(r[1]);
            row.turns  = std::stoll(r[2]);
        } catch (...) {}
        out.push_back(std::move(row));
    });
    return out;
}

} // namespace icmg::cli
