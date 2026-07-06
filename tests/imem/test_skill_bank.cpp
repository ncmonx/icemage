// TDD (2026-07-06): trajectory -> skill-bank distillation (feature #7 from
// docs/plans/2026-07-04-feature-research-2026-landscape.md).
//
// Deterministic distillation of successful command trajectories into a bounded,
// stable-size reusable skill bank, with causal attribution ("what worked" =
// success rate). Pure core: given per-command trajectory stats, rank + bound
// the skills. No LLM. Extends learn / verifications data, not a parallel build.
#include "../test_main.hpp"
#include "../../src/imem/skill_bank.hpp"
#include <vector>
#include <string>

using icmg::imem::TrajectoryStat;
using icmg::imem::Skill;
using icmg::imem::distillSkills;

static std::vector<TrajectoryStat> sample() {
    return {
        {"cmake --build build", 10, 10},   // 100% success, frequent -> strong skill
        {"ctest --output-on-failure", 8, 7}, // 87.5%
        {"git push --force", 3, 0},         // 0% success -> NOT a skill
        {"pwsh -File build.ps1", 5, 5},     // 100% but less frequent
        {"echo hi", 1, 1},                  // 100% but freq below floor -> excluded
    };
}

// 1. Only commands that succeed and clear the frequency floor become skills.
TEST("skillbank: distills only repeatedly-successful commands") {
    auto skills = distillSkills(sample(), /*maxSkills=*/10, /*minFreq=*/2, /*minRate=*/0.5);
    // git push --force (0%) and echo hi (freq 1) excluded.
    bool hasPush = false, hasEcho = false;
    for (auto& s : skills) {
        if (s.command == "git push --force") hasPush = true;
        if (s.command == "echo hi") hasEcho = true;
    }
    ASSERT_TRUE(!hasPush);
    ASSERT_TRUE(!hasEcho);
    ASSERT_TRUE(skills.size() == 3);
}

// 2. Causal attribution: each skill carries its success rate.
TEST("skillbank: attaches success-rate attribution") {
    auto skills = distillSkills(sample(), 10, 2, 0.5);
    for (auto& s : skills) {
        if (s.command == "cmake --build build") {
            ASSERT_TRUE(s.success_rate > 0.99);
            ASSERT_EQ(s.uses, 10);
        }
    }
}

// 3. Ranking: strongest (rate then frequency) first.
TEST("skillbank: ranks by success then frequency") {
    auto skills = distillSkills(sample(), 10, 2, 0.5);
    ASSERT_TRUE(!skills.empty());
    // cmake (100%, freq10) must rank above ctest (87.5%).
    ASSERT_EQ(skills.front().command, std::string("cmake --build build"));
}

// 4. Bounded: maxSkills caps the bank to a stable size.
TEST("skillbank: bounded to maxSkills") {
    auto skills = distillSkills(sample(), /*maxSkills=*/2, 2, 0.5);
    ASSERT_TRUE(skills.size() == 2);
}

// 5. Empty trajectory -> empty bank (no crash).
TEST("skillbank: empty input yields empty bank") {
    auto skills = distillSkills({}, 10, 2, 0.5);
    ASSERT_TRUE(skills.empty());
}
