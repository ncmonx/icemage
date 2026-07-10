// Gap #3: real Anthropic API token ledger. savings was a proxy estimate; the
// ledger stores the actual usage block (input/output/cache) the API returns.
// These tests exercise the testable helper (ensure/record/aggregate) directly.
#include "../test_main.hpp"
#include "../../src/cli/token_ledger.hpp"
#include "../../src/core/db.hpp"

using namespace icmg::cli;

TEST("token-ledger: ensure creates table; aggregate empty is zero") {
    icmg::core::Db db(":memory:");
    auto t = aggregateTokenLedger(db, 0);  // ensure + read
    ASSERT_EQ((long long)t.rows, 0LL);
    ASSERT_EQ((long long)t.input, 0LL);
    ASSERT_EQ((long long)t.output, 0LL);
}

TEST("token-ledger: record + aggregate sums all four token kinds") {
    icmg::core::Db db(":memory:");
    TokenLedgerEntry a; a.input_tokens = 100; a.output_tokens = 20;
    a.cache_read_tokens = 50; a.cache_creation_tokens = 10; a.model = "sonnet";
    TokenLedgerEntry b; b.input_tokens = 200; b.output_tokens = 40;
    b.cache_read_tokens = 0;  b.cache_creation_tokens = 5;
    ASSERT_TRUE(recordTokenLedger(db, a));
    ASSERT_TRUE(recordTokenLedger(db, b));

    auto t = aggregateTokenLedger(db, 0);
    ASSERT_EQ((long long)t.rows, 2LL);
    ASSERT_EQ((long long)t.input, 300LL);
    ASSERT_EQ((long long)t.output, 60LL);
    ASSERT_EQ((long long)t.cache_read, 50LL);
    ASSERT_EQ((long long)t.cache_creation, 15LL);
    ASSERT_EQ((long long)t.totalInput(), 365LL);  // input + cache_read + cache_creation
}

TEST("token-ledger: a fully-zero usage row is skipped (not recorded)") {
    icmg::core::Db db(":memory:");
    TokenLedgerEntry zero;  // all zero
    ASSERT_FALSE(recordTokenLedger(db, zero));
    auto t = aggregateTokenLedger(db, 0);
    ASSERT_EQ((long long)t.rows, 0LL);
}

TEST("token-ledger: window filter excludes out-of-window rows") {
    icmg::core::Db db(":memory:");
    ensureTokenLedger(db);
    // One in-window (auto ts=now) and one stamped 10 days ago.
    TokenLedgerEntry now_e; now_e.input_tokens = 100;
    ASSERT_TRUE(recordTokenLedger(db, now_e));
    int64_t old_ts = (int64_t)std::time(nullptr) - 10 * 86400;
    db.run("INSERT INTO token_ledger (ts, input_tokens) VALUES (?, ?)",
           {std::to_string(old_ts), "999"});

    auto all  = aggregateTokenLedger(db, 0);   // all time
    auto win  = aggregateTokenLedger(db, 7);   // last 7 days
    ASSERT_EQ((long long)all.input, 1099LL);
    ASSERT_EQ((long long)win.input, 100LL);    // old row excluded
}

// Bug report 2026-07-10 (icmg-savings-daily-history.md): the "Daily real-token
// history" block in `icmg savings` read from a DIFFERENT source
// (context-budget --all-sessions transcript-mtime estimate) than the
// "Real API tokens" headline (token_ledger) -- causing missing days + wrong
// magnitudes. Fix: a pure day-bucketed aggregation directly over token_ledger,
// the SAME source of truth as the headline block.
TEST("token-ledger-by-day: empty table yields no rows") {
    icmg::core::Db db(":memory:");
    auto rows = aggregateTokenLedgerByDay(db, 30);
    ASSERT_EQ((int)rows.size(), 0);
}

TEST("token-ledger-by-day: sums all four token kinds per day") {
    icmg::core::Db db(":memory:");
    ensureTokenLedger(db);
    int64_t today = (int64_t)std::time(nullptr);
    db.run("INSERT INTO token_ledger (ts, input_tokens, output_tokens,"
           " cache_read_tokens, cache_creation_tokens) VALUES (?, 100, 20, 50, 10)",
           {std::to_string(today)});
    db.run("INSERT INTO token_ledger (ts, input_tokens, output_tokens,"
           " cache_read_tokens, cache_creation_tokens) VALUES (?, 200, 40, 0, 5)",
           {std::to_string(today)});
    auto rows = aggregateTokenLedgerByDay(db, 30);
    ASSERT_EQ((int)rows.size(), 1);
    // 100+20+50+10 + 200+40+0+5 = 180 + 245 = 425
    ASSERT_EQ((long long)rows[0].tokens, 425LL);
    ASSERT_EQ((long long)rows[0].turns, 2LL);
}

TEST("token-ledger-by-day: distinct days are separate rows, newest first") {
    icmg::core::Db db(":memory:");
    ensureTokenLedger(db);
    int64_t now = (int64_t)std::time(nullptr);
    int64_t yesterday = now - 86400;
    int64_t two_days_ago = now - 2 * 86400;
    db.run("INSERT INTO token_ledger (ts, input_tokens) VALUES (?, 10)",
           {std::to_string(two_days_ago)});
    db.run("INSERT INTO token_ledger (ts, input_tokens) VALUES (?, 20)",
           {std::to_string(yesterday)});
    db.run("INSERT INTO token_ledger (ts, input_tokens) VALUES (?, 30)",
           {std::to_string(now)});
    auto rows = aggregateTokenLedgerByDay(db, 30);
    ASSERT_EQ((int)rows.size(), 3);
    ASSERT_EQ((long long)rows[0].tokens, 30LL);  // newest first
    ASSERT_EQ((long long)rows[2].tokens, 10LL);  // oldest last
}

TEST("token-ledger-by-day: max_rows caps the output (still newest-first)") {
    icmg::core::Db db(":memory:");
    ensureTokenLedger(db);
    int64_t now = (int64_t)std::time(nullptr);
    for (int i = 0; i < 5; ++i) {
        int64_t ts = now - (int64_t)i * 86400;
        db.run("INSERT INTO token_ledger (ts, input_tokens) VALUES (?, ?)",
               {std::to_string(ts), std::to_string(100 * (i + 1))});
    }
    auto rows = aggregateTokenLedgerByDay(db, 2);
    ASSERT_EQ((int)rows.size(), 2);
    ASSERT_EQ((long long)rows[0].tokens, 100LL);  // today (i=0) newest
}

// Regression guard for the exact reported symptom: a session spanning several
// turns whose activity is spread across a busy day must NOT vanish (the old
// bug bucketed by transcript-file mtime, not per-row ts, so busy days with a
// still-open file could disappear). Multiple rows on the SAME day must always
// reconcile to one bucket whose sum equals every row inserted that day.
TEST("token-ledger-by-day: busy day with many turns is never dropped") {
    icmg::core::Db db(":memory:");
    ensureTokenLedger(db);
    int64_t now = (int64_t)std::time(nullptr);
    int64_t expected = 0;
    for (int i = 0; i < 50; ++i) {
        db.run("INSERT INTO token_ledger (ts, input_tokens, output_tokens)"
               " VALUES (?, 1000, 500)", {std::to_string(now)});
        expected += 1500;
    }
    auto rows = aggregateTokenLedgerByDay(db, 30);
    ASSERT_EQ((int)rows.size(), 1);
    ASSERT_EQ((long long)rows[0].tokens, expected);
    ASSERT_EQ((long long)rows[0].turns, 50LL);
}

TEST("token-ledger: cacheHitRate = cache_read / totalInput (0 when empty)") {
    // Empty totals -> no divide-by-zero, rate is 0.
    TokenLedgerTotals empty;
    ASSERT_TRUE(empty.cacheHitRate() == 0.0);

    // input=300, cache_read=50, cache_creation=15 -> totalInput=365.
    // hit rate = 50/365 = 0.13698...
    TokenLedgerTotals t;
    t.input = 300; t.cache_read = 50; t.cache_creation = 15;
    double r = t.cacheHitRate();
    ASSERT_TRUE(r > 0.136 && r < 0.138);

    // All-cache-read context -> rate ~1.0 (best case).
    TokenLedgerTotals hot;
    hot.cache_read = 1000;  // input=0, creation=0
    ASSERT_TRUE(hot.cacheHitRate() > 0.999);
}
