// 2026-06-07: context-budget gauge (pure helpers).
#include "../test_main.hpp"
#include "../../src/cli/context_budget.hpp"
#include <fstream>
#include <filesystem>
using namespace icmg::cli;

static const std::string USAGE =
  R"("usage":{"input_tokens":124,"cache_creation_input_tokens":3881,"cache_read_input_tokens":562599,"output_tokens":458})";

TEST("context_budget: pct math + clamps") {
    auto b = computeBudget(566604, 1000000);
    ASSERT_EQ(b.pctUsed, 56); ASSERT_EQ(b.pctLeft, 44);
    auto z = computeBudget(0, 1000000);     ASSERT_EQ(z.pctUsed, 0);   ASSERT_EQ(z.pctLeft, 100);
    auto o = computeBudget(2000000, 1000000); ASSERT_EQ(o.pctUsed, 100); ASSERT_EQ(o.pctLeft, 0);
    auto n = computeBudget(100, 0);          ASSERT_EQ(n.pctUsed, 0);   ASSERT_EQ(n.pctLeft, 100); // no limit safe
}

TEST("context_budget: extractLL exact key (not substring)") {
    ASSERT_EQ(extractLL(USAGE, "input_tokens"), 124LL);                   // not the cache_* ones
    ASSERT_EQ(extractLL(USAGE, "cache_creation_input_tokens"), 3881LL);
    ASSERT_EQ(extractLL(USAGE, "cache_read_input_tokens"), 562599LL);
    ASSERT_EQ(extractLL(USAGE, "missing"), 0LL);
}

TEST("context_budget: contextTokens sums 3 inputs (excludes output)") {
    ASSERT_EQ(contextTokensFromUsageLine(USAGE), 566604LL);  // 124+3881+562599, not +458
}

TEST("context_budget: format string") {
    ASSERT_CONTAINS(formatBudget(computeBudget(566604, 1000000)), "56% used");
    ASSERT_CONTAINS(formatBudget(computeBudget(566604, 1000000)), "44% left");
}

TEST("context_budget: lastContextTokensFromTranscript reads LAST usage") {
    auto p = std::filesystem::temp_directory_path() / "icmg-ctxbud-test.jsonl";
    { std::ofstream f(p);
      f << "{\"usage\":{\"input_tokens\":10,\"cache_read_input_tokens\":20}}\n";
      f << "{\"unrelated\":1}\n";
      f << "{\"usage\":{\"input_tokens\":100,\"cache_creation_input_tokens\":5,\"cache_read_input_tokens\":900}}\n"; }
    ASSERT_EQ(lastContextTokensFromTranscript(p.string()), 1005LL);  // last line wins: 100+5+900
    std::filesystem::remove(p);
    ASSERT_EQ(lastContextTokensFromTranscript("Z:/nope/none.jsonl"), 0LL);  // missing -> 0
}

// 2026-06-10: model context-window registry -- makes the budget meter honest
// per-model instead of a hardcoded 1M (which lies on 128K/200K models).
TEST("model_context: Claude Code 1M-window families") {
    ASSERT_EQ(modelContextWindow("claude-opus-4-8"), 1000000LL);
    ASSERT_EQ(modelContextWindow("claude-sonnet-4-6"), 1000000LL);
    // substring-match: full vendor-prefixed / date-suffixed ids still resolve
    ASSERT_EQ(modelContextWindow("anthropic/claude-opus-4-8-20260101"), 1000000LL);
    ASSERT_EQ(modelContextWindow("gemini-1.5-pro"), 1000000LL);
}

TEST("model_context: 128K-window families") {
    ASSERT_EQ(modelContextWindow("gpt-4o-2024-11-20"), 128000LL);
    ASSERT_EQ(modelContextWindow("gpt-4-turbo"), 128000LL);
    ASSERT_EQ(modelContextWindow("deepseek-chat"), 128000LL);
    ASSERT_EQ(modelContextWindow("mistral-large-latest"), 128000LL);
}

TEST("model_context: default 200K for unknown / 200K-window / synthetic") {
    ASSERT_EQ(modelContextWindow("claude-haiku-4-5"), 200000LL);  // Haiku = 200K, not opus/sonnet-4
    ASSERT_EQ(modelContextWindow("o1-preview"), 200000LL);
    ASSERT_EQ(modelContextWindow("some-future-llm-xyz"), 200000LL);
    ASSERT_EQ(modelContextWindow("<synthetic>"), 200000LL);       // CC synthetic turns
    ASSERT_EQ(modelContextWindow(""), 200000LL);
}

TEST("model_context: lastModelFromTranscript skips synthetic, takes last real") {
    auto p = std::filesystem::temp_directory_path() / "icmg-modelctx-test.jsonl";
    { std::ofstream f(p);
      f << "{\"message\":{\"model\":\"claude-sonnet-4-6\"}}\n";
      f << "{\"message\":{\"model\":\"<synthetic>\"}}\n";          // must be skipped
      f << "{\"message\":{\"model\":\"claude-opus-4-8\"}}\n";      // last real wins
      f << "{\"unrelated\":1}\n"; }
    ASSERT_EQ(lastModelFromTranscript(p.string()), std::string("claude-opus-4-8"));
    std::filesystem::remove(p);
    ASSERT_EQ(lastModelFromTranscript("Z:/nope/none.jsonl"), std::string(""));  // missing -> empty
}
