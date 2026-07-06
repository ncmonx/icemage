// TDD (2026-07-06): pre-flight prompt rewrite (feature #8), built on the TE2
// salience compressor (feature #6 foundation) in core/compress_select.hpp.
//
// `icmg agent --rewrite` compresses the bulky PACKED-CONTEXT portion of the
// assembled prompt while leaving the system prompt + task instruction
// UNTOUCHED, with an honesty gate: if compression doesn't actually shrink the
// context it is discarded (never blow up the prompt). Pure + deterministic
// (no LLM), so the English-code caveat from the plan doesn't bite.
#include "../test_main.hpp"
#include "../../src/core/prompt_rewrite.hpp"
#include <string>

using icmg::core::RewriteReport;
using icmg::core::compressContext;
using icmg::core::rewriteAssembled;
using icmg::core::estimateTokens;

static std::string bulky() {
    // 20 lines: half rich/identifier-dense, half boilerplate dashes/blank.
    std::string s;
    for (int i = 0; i < 10; ++i)
        s += "function processPayment handles refundGateway retryPolicy line" + std::to_string(i) + "\n";
    for (int i = 0; i < 10; ++i)
        s += "----------\n";
    return s;
}

// 1. Over-budget context shrinks; report marks it applied.
TEST("rewrite: over-budget context is compressed and reported") {
    std::string ctx = bulky();
    RewriteReport rep;
    std::string out = compressContext(ctx, /*budgetChars=*/300, rep);
    ASSERT_TRUE(rep.applied);
    ASSERT_TRUE(rep.after_chars < rep.before_chars);
    ASSERT_TRUE(out.size() < ctx.size());
}

// 2. Honesty gate: already-small context is returned unchanged, applied=false.
TEST("rewrite: under-budget context is left untouched") {
    std::string ctx = "just one short line\n";
    RewriteReport rep;
    std::string out = compressContext(ctx, /*budgetChars=*/4096, rep);
    ASSERT_TRUE(!rep.applied);
    ASSERT_EQ(out, ctx);
}

// 3. Compression keeps the high-salience lines, drops boilerplate.
TEST("rewrite: keeps identifier-rich lines, drops dash boilerplate") {
    std::string ctx = bulky();
    RewriteReport rep;
    std::string out = compressContext(ctx, 300, rep);
    ASSERT_CONTAINS(out, "processPayment");     // rich survives
    ASSERT_NOT_CONTAINS(out, "----------");     // boilerplate dropped
}

// 4. Never returns empty for non-empty input (top span always survives).
TEST("rewrite: never empties non-empty context even at tiny budget") {
    std::string ctx = bulky();
    RewriteReport rep;
    std::string out = compressContext(ctx, /*budgetChars=*/1, rep);
    ASSERT_TRUE(!out.empty());
}

// 5. rewriteAssembled protects head (system) + tail (task) EXACTLY, compresses
//    only the middle context.
TEST("rewrite: rewriteAssembled protects system prompt and task verbatim") {
    std::string head = "SYSTEM: you are an agent. Follow rules EXACTLY.\n\n";
    std::string body = bulky();
    std::string tail = "## Task\nrefactor the retry logic\n";
    RewriteReport rep;
    std::string out = rewriteAssembled(head, body, tail, /*budgetChars=*/300, rep);
    // protected regions appear verbatim
    ASSERT_CONTAINS(out, "you are an agent. Follow rules EXACTLY.");
    ASSERT_CONTAINS(out, "## Task\nrefactor the retry logic");
    // and the whole thing got smaller than naive concatenation
    ASSERT_TRUE(out.size() < (head + body + tail).size());
    ASSERT_TRUE(rep.applied);
}

// 6. estimateTokens is monotonic and ~chars/4.
TEST("rewrite: estimateTokens grows with length") {
    ASSERT_TRUE(estimateTokens(std::string(400, 'x')) > estimateTokens(std::string(40, 'x')));
    ASSERT_TRUE(estimateTokens("") == 0);
}
