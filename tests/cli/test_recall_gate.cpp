// Pre-task recall gate — pure-core unit tests (TDD-first, 2026-06-11).
// Block iff (complex task) AND (no recall/pack this turn).

#include "../test_main.hpp"
#include "../../src/cli/recall_gate.hpp"

using namespace icmg::cli;

TEST("recall-gate: complex + no recall -> BLOCK") {
    auto v = recallGateVerdict(/*complex=*/true, /*recalled=*/false);
    ASSERT_TRUE(v.block);
    ASSERT_FALSE(v.reason.empty());
}

TEST("recall-gate: complex + recalled -> allow") {
    auto v = recallGateVerdict(true, true);
    ASSERT_FALSE(v.block);
}

TEST("recall-gate: simple + no recall -> allow (one-liners need no context)") {
    auto v = recallGateVerdict(false, false);
    ASSERT_FALSE(v.block);
}

TEST("recall-gate: simple + recalled -> allow") {
    auto v = recallGateVerdict(false, true);
    ASSERT_FALSE(v.block);
}
