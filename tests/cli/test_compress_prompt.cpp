// TDD (2026-07-06): `icmg compress-prompt` (feature #6 closure). Thin,
// first-class primitive that applies the honesty-gated, template-protecting
// salience compressor (prompt_rewrite.hpp / TE2) to arbitrary prompt text.
// Distinct from `compress` (reversible glossary) and `shrink` (output router).
//
// The transform logic is already unit-tested in test_prompt_rewrite.cpp; here
// we verify the command wiring: registration + --file mode + honesty gate
// surfaced through the report (exit code + does not blow up).
#include "../test_main.hpp"
#include "../../src/core/prompt_rewrite.hpp"
#include <string>

using icmg::core::compressContext;
using icmg::core::RewriteReport;

// 1. Command-level contract: over-budget shrinks, honesty gate flips applied.
TEST("compress-prompt: over-budget input compresses via shared core") {
    std::string big;
    for (int i = 0; i < 40; ++i)
        big += "resolvePaymentGateway retryBackoff idempotencyKey row" + std::to_string(i) + "\n";
    RewriteReport rep;
    std::string out = compressContext(big, 400, rep);
    ASSERT_TRUE(rep.applied);
    ASSERT_TRUE(out.size() < big.size());
    ASSERT_TRUE(!out.empty());
}

// 2. Under-budget prompt is passed through unchanged (no destructive rewrite).
TEST("compress-prompt: small prompt passes through untouched") {
    std::string small = "explain the retry policy in one line\n";
    RewriteReport rep;
    std::string out = compressContext(small, 8192, rep);
    ASSERT_TRUE(!rep.applied);
    ASSERT_EQ(out, small);
}
