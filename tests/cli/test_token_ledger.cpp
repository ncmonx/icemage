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
