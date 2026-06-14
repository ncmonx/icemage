// `icmg savings --daily` (2026-06-14): console per-day saved-token breakdown.
// Before this, per-day savings was only visible in --html (SVG chart); the
// plain console showed only the window aggregate. formatDailySavingsRows is the
// pure render step (newest-first, capped), tested here without a DB.
#include "../test_main.hpp"
#include "../../src/cli/savings_daily.hpp"

using namespace icmg::cli;

// ---- Test 1: empty map -> no rows -------------------------------------------
TEST("savings_daily: empty map yields no rows") {
    std::map<std::string, int64_t> by_day;
    ASSERT_EQ((int)formatDailySavingsRows(by_day, 14).size(), 0);
}

// ---- Test 2: newest date first ----------------------------------------------
TEST("savings_daily: rows are newest-first") {
    std::map<std::string, int64_t> by_day{
        {"2026-06-12", 100}, {"2026-06-13", 200}, {"2026-06-14", 300}};
    auto rows = formatDailySavingsRows(by_day, 14);
    ASSERT_EQ((int)rows.size(), 3);
    ASSERT_CONTAINS(rows[0], "2026-06-14");   // newest first
    ASSERT_CONTAINS(rows[0], "300");
    ASSERT_CONTAINS(rows[2], "2026-06-12");   // oldest last
}

// ---- Test 3: maxRows caps the output ----------------------------------------
TEST("savings_daily: maxRows caps output") {
    std::map<std::string, int64_t> by_day{
        {"2026-06-10", 1}, {"2026-06-11", 2}, {"2026-06-12", 3},
        {"2026-06-13", 4}, {"2026-06-14", 5}};
    auto rows = formatDailySavingsRows(by_day, 2);
    ASSERT_EQ((int)rows.size(), 2);
    ASSERT_CONTAINS(rows[0], "2026-06-14");   // still newest-first within cap
    ASSERT_CONTAINS(rows[1], "2026-06-13");
}

// ---- Test 4: maxRows <= 0 -> empty ------------------------------------------
TEST("savings_daily: non-positive maxRows yields no rows") {
    std::map<std::string, int64_t> by_day{{"2026-06-14", 5}};
    ASSERT_EQ((int)formatDailySavingsRows(by_day, 0).size(), 0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
