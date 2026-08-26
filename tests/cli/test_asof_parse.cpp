// TDD (2026-08-25): --as-of value parsing (brain v2.22 #1). Pure, no IO.
#include "../test_main.hpp"
#include "../../src/cli/asof_parse.hpp"

using icmg::cli::parseAsOf;
using icmg::cli::daysFromCivil;

static const int64_t kNow = 1756100000; // fixed fake "now" for relative forms

TEST("asof_parse: plain epoch passes through") {
    ASSERT_EQ(parseAsOf("1700000000", kNow), (int64_t)1700000000);
}

TEST("asof_parse: relative 7d/24h/30m = that long ago") {
    ASSERT_EQ(parseAsOf("7d", kNow), kNow - 7 * 86400);
    ASSERT_EQ(parseAsOf("24h", kNow), kNow - 24 * 3600);
    ASSERT_EQ(parseAsOf("30m", kNow), kNow - 30 * 60);
}

TEST("asof_parse: YYYY-MM-DD is midnight UTC") {
    // 2026-01-01 00:00:00 UTC = 1767225600
    ASSERT_EQ(parseAsOf("2026-01-01", kNow), (int64_t)1767225600);
    // Epoch day zero sanity.
    ASSERT_EQ(daysFromCivil(1970, 1, 1), (int64_t)0);
}

TEST("asof_parse: garbage yields 0") {
    ASSERT_EQ(parseAsOf("", kNow), (int64_t)0);
    ASSERT_EQ(parseAsOf("yesterday", kNow), (int64_t)0);
    ASSERT_EQ(parseAsOf("2026-13-40", kNow), (int64_t)0);
    ASSERT_EQ(parseAsOf("-5d", kNow), (int64_t)0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
