// 2026-06-07: context-budget gauge (pure helpers).
// 2026-06-10: + model context-window registry + model pricing registry (folded
// here, same budget-meter concern, to avoid a new CMakeLists test target).
#include "../test_main.hpp"
#include "../../src/cli/context_budget.hpp"
#include "../../src/cli/model_pricing.hpp"
#include "../../src/core/intent_slice.hpp"
#include "../../src/core/read_dedup.hpp"
#include <fstream>
#include <filesystem>
#include <cmath>
using namespace icmg::cli;

// Compare $/MTok as integer cents to avoid double-eq fragility.
static long long mp_cents(double d) { return (long long)(d * 100.0 + 0.5); }

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

// --- model pricing registry (as-of 2026-06; override-able) ---------------
TEST("model_pricing: Claude tiers (in/out $/MTok)") {
    ASSERT_EQ(mp_cents(modelPricing("claude-opus-4-8").in),  1500LL);
    ASSERT_EQ(mp_cents(modelPricing("claude-opus-4-8").out), 7500LL);
    ASSERT_EQ(mp_cents(modelPricing("claude-sonnet-4-6").in),  300LL);
    ASSERT_EQ(mp_cents(modelPricing("claude-sonnet-4-6").out), 1500LL);
    ASSERT_EQ(mp_cents(modelPricing("claude-haiku-4-5").in),  100LL);
    ASSERT_EQ(mp_cents(modelPricing("claude-haiku-4-5").out), 500LL);
}

TEST("model_pricing: substring-match on vendor-prefixed / dated ids") {
    ASSERT_EQ(mp_cents(modelPricing("anthropic/claude-opus-4-8-20260101").in), 1500LL);
}

TEST("model_pricing: non-Claude tiers") {
    ASSERT_EQ(mp_cents(modelPricing("gpt-4o-2024-11-20").in),  250LL);
    ASSERT_EQ(mp_cents(modelPricing("gpt-4o-2024-11-20").out), 1000LL);
    ASSERT_EQ(mp_cents(modelPricing("o1-preview").in),  1500LL);
    ASSERT_EQ(mp_cents(modelPricing("o1-preview").out), 6000LL);
    ASSERT_EQ(mp_cents(modelPricing("gemini-1.5-pro").in),  125LL);
    ASSERT_EQ(mp_cents(modelPricing("gemini-1.5-pro").out), 500LL);
}

TEST("model_pricing: default = Sonnet for unknown / empty / synthetic") {
    ASSERT_EQ(mp_cents(modelPricing("some-future-llm-xyz").in),  300LL);
    ASSERT_EQ(mp_cents(modelPricing("some-future-llm-xyz").out), 1500LL);
    ASSERT_EQ(mp_cents(modelPricing("").in),  300LL);
    ASSERT_EQ(mp_cents(modelPricing("<synthetic>").out), 1500LL);
}

// --- intent slice: semantic single-file slice (icmg context --for) -------
// 10-line fixture; line 2 + line 8 carry "rate".
static const std::string SLICE_BODY =
    "alpha one\n"          // 1
    "beta rate two\n"      // 2  hit: rate
    "gamma three\n"        // 3
    "delta four\n"         // 4  hit: four
    "epsilon five\n"       // 5  hit: five
    "zeta six\n"           // 6
    "eta seven\n"          // 7
    "theta rate eight\n"   // 8  hit: rate, eight
    "iota nine\n"          // 9
    "kappa ten\n";         // 10

TEST("intent_slice: two distant hits -> two windows in reading order") {
    auto r = icmg::core::intentSliceRanges(SLICE_BODY, "rate", /*ctx*/1, /*maxRanges*/4, /*maxTotal*/80);
    ASSERT_EQ((int)r.size(), 2);
    ASSERT_EQ(r[0].start, 1); ASSERT_EQ(r[0].end, 3);
    ASSERT_EQ(r[1].start, 7); ASSERT_EQ(r[1].end, 9);
}

TEST("intent_slice: adjacent hits merge into one window") {
    auto r = icmg::core::intentSliceRanges(SLICE_BODY, "four five", 1, 4, 80);
    ASSERT_EQ((int)r.size(), 1);
    ASSERT_EQ(r[0].start, 3); ASSERT_EQ(r[0].end, 6);
}

TEST("intent_slice: top-ranked window wins under maxRanges cap") {
    // line 8 scores 2 (rate+eight) > line 2 scores 1 (rate) -> keep line-8 window.
    auto r = icmg::core::intentSliceRanges(SLICE_BODY, "rate eight", 1, 1, 80);
    ASSERT_EQ((int)r.size(), 1);
    ASSERT_EQ(r[0].start, 7); ASSERT_EQ(r[0].end, 9);
}

TEST("intent_slice: no match -> empty; stopwords/short terms dropped") {
    ASSERT_EQ((int)icmg::core::intentSliceRanges(SLICE_BODY, "zzz", 1, 4, 80).size(), 0);
    // "a"/"the" dropped -> behaves exactly like the bare "rate" query
    auto r = icmg::core::intentSliceRanges(SLICE_BODY, "a the rate", 1, 4, 80);
    ASSERT_EQ((int)r.size(), 2);
    ASSERT_EQ(r[0].start, 1); ASSERT_EQ(r[1].start, 7);
}

// --- read dedup: stub on context cache HIT (already-shown body) -----------
TEST("read_dedup: stub only for big bodies, never when forced full") {
    ASSERT_TRUE(icmg::core::shouldStubContext(2000, /*forceFull*/false));
    ASSERT_TRUE(icmg::core::shouldStubContext(400, false));    // boundary (>=)
    ASSERT_TRUE(!icmg::core::shouldStubContext(399, false));   // too small
    ASSERT_TRUE(!icmg::core::shouldStubContext(100, false));
    ASSERT_TRUE(!icmg::core::shouldStubContext(2000, true));   // --full forces full
}

TEST("read_dedup: stub text names file + escape hatch + bytes skipped") {
    std::string s = icmg::core::contextSeenStub("src/foo.cpp", 2048);
    ASSERT_CONTAINS(s, "src/foo.cpp");
    ASSERT_CONTAINS(s, "unchanged");
    ASSERT_CONTAINS(s, "--full");
    ASSERT_CONTAINS(s, "2048");
}
