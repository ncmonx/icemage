// `icmg badge` pure formatter unit tests (2026-07-01).
// Spec for badge_core.hpp: threshold color + humanized numbers + empty-data
// guard + savings --json parse. No subprocess (command wiring smoke-tested
// separately in Task 5).
#include "../test_main.hpp"
#include "../../src/cli/badge_core.hpp"

using namespace icmg::cli;

// --- Task 1: savings metric render + threshold color ---

TEST("badge: savings pct brightgreen at 99.3") {
    BadgeData d; d.pct = 99.3; d.saved_tokens = 10367462; d.cost_saved = 23.2; d.total_calls = 2726;
    std::string j = renderBadge("savings", d);
    ASSERT_TRUE(j.find("\"schemaVersion\":1") != std::string::npos);
    ASSERT_TRUE(j.find("\"label\":\"token saved\"") != std::string::npos);
    ASSERT_TRUE(j.find("\"message\":\"99.3%\"") != std::string::npos);
    ASSERT_TRUE(j.find("\"color\":\"brightgreen\"") != std::string::npos);
}

TEST("badge: savings pct thresholds") {
    BadgeData d; d.total_calls = 10;
    d.pct = 75.0; ASSERT_TRUE(renderBadge("savings", d).find("\"color\":\"green\"") != std::string::npos);
    d.pct = 55.0; ASSERT_TRUE(renderBadge("savings", d).find("\"color\":\"yellow\"") != std::string::npos);
    d.pct = 35.0; ASSERT_TRUE(renderBadge("savings", d).find("\"color\":\"orange\"") != std::string::npos);
    d.pct = 10.0; ASSERT_TRUE(renderBadge("savings", d).find("\"color\":\"red\"") != std::string::npos);
}

// --- Task 2: tokens + cost metrics with humanized numbers ---

TEST("badge: tokens humanized + saved value") {
    BadgeData d; d.saved_tokens = 10367462; d.total_calls = 2726;
    std::string j = renderBadge("tokens", d);
    ASSERT_TRUE(j.find("\"label\":\"tokens saved\"") != std::string::npos);
    ASSERT_TRUE(j.find("\"message\":\"10.4M\"") != std::string::npos);
    ASSERT_TRUE(j.find("\"color\":\"brightgreen\"") != std::string::npos);
}

TEST("badge: tokens k-scale") {
    BadgeData d; d.saved_tokens = 483389; d.total_calls = 10;
    ASSERT_TRUE(renderBadge("tokens", d).find("\"message\":\"483k\"") != std::string::npos);
    ASSERT_TRUE(renderBadge("tokens", d).find("\"color\":\"green\"") != std::string::npos);
}

TEST("badge: cost dollar one decimal") {
    BadgeData d; d.cost_saved = 23.2; d.total_calls = 10;
    std::string j = renderBadge("cost", d);
    ASSERT_TRUE(j.find("\"label\":\"cost saved\"") != std::string::npos);
    ASSERT_TRUE(j.find("\"message\":\"$23.2\"") != std::string::npos);
    ASSERT_TRUE(j.find("\"color\":\"brightgreen\"") != std::string::npos);
}

// --- Task 3: empty-data guard ---

TEST("badge: empty data -> no data lightgrey") {
    BadgeData d; // total_calls = 0
    for (const char* m : {"savings", "tokens", "cost"}) {
        std::string j = renderBadge(m, d);
        ASSERT_TRUE(j.find("\"message\":\"no data\"") != std::string::npos);
        ASSERT_TRUE(j.find("\"color\":\"lightgrey\"") != std::string::npos);
    }
}

// --- Task 4: parseSavingsJson field extraction ---

TEST("badge: parseSavingsJson extracts fields") {
    const char* sample = R"({"window_days":30,"total":{"calls":2726,"raw":10438997,"actual":483389,"saved":10367462,"pct":99.3},"cost":{"without":24.6,"with":1.4,"saved":23.2}})";
    BadgeData d = parseSavingsJson(sample);
    ASSERT_EQ(d.total_calls, 2726LL);
    ASSERT_EQ(d.saved_tokens, 10367462LL);
    ASSERT_TRUE(d.pct > 99.2 && d.pct < 99.4);
    ASSERT_TRUE(d.cost_saved > 23.1 && d.cost_saved < 23.3);
}

// --- Regression: BLOCKER caught by craftsman:challenge (2026-07-01) ---
// `savings` reads its window via --window (exact-match flagValue), NOT
// --window-days. If badge spawned --window-days, the window was silently
// ignored -> wrong default-30 data in an advertised flag. Lock the translation.
TEST("badge: savingsArgv translates window-days -> savings --window") {
    auto argv = savingsArgv("icmg", "7");
    // exact expected argv, in order
    std::vector<std::string> want = {"icmg", "savings", "--json", "--window", "7"};
    ASSERT_TRUE(argv == want);
    // must NOT emit the broken --window-days token
    for (const auto& a : argv) ASSERT_TRUE(a != "--window-days");
}

TEST("badge: savingsArgv carries the exe path (selfExePath, not bare icmg)") {
    auto argv = savingsArgv("C:/build/icmg.exe", "30");
    ASSERT_EQ(argv[0], std::string("C:/build/icmg.exe"));
    ASSERT_EQ(argv[4], std::string("30"));
}
