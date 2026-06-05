// estimateTokens — char-class weighted heuristic. Not exact (Claude's tokenizer
// is not public), but accounts for code density (punct/digits tokenize denser
// than prose) so it beats a uniform bytes/4 estimate. Tests assert *behavior*
// (ranges + relative ordering), not brittle exact counts.
#include "../test_main.hpp"
#include "../../src/core/token_counter.hpp"

using icmg::core::estimateTokens;

TEST("estimateTokens: empty -> 0") {
    ASSERT_EQ(estimateTokens(""), (size_t)0);
}

TEST("estimateTokens: pure whitespace -> 0 (not inflated)") {
    ASSERT_EQ(estimateTokens("     \n\n\t  "), (size_t)0);
}

TEST("estimateTokens: whitespace between words is absorbed, not counted") {
    // 8 letters spaced vs 8 letters joined -> roughly equal (whitespace free).
    size_t spaced = estimateTokens("aaaa aaaa");
    size_t joined = estimateTokens("aaaaaaaa");
    ASSERT_TRUE(spaced == joined);
}

TEST("estimateTokens: punctuation-dense counts more than pure letters at equal length") {
    size_t letters = estimateTokens("aaaaaaaa");   // 8 letters
    size_t code    = estimateTokens("a;a;a;a;");   // 4 letters + 4 punct (same byte length)
    ASSERT_TRUE(code > letters);                   // code tokenizes denser than prose
}

TEST("estimateTokens: monotonic in content length") {
    ASSERT_TRUE(estimateTokens("hello world foo bar baz") > estimateTokens("hello"));
}

TEST("estimateTokens: prose lands in a sane chars/4 ballpark") {
    // "the quick brown fox" = 16 letters; real BPE tokenizers give ~4.
    size_t t = estimateTokens("the quick brown fox");
    ASSERT_TRUE(t >= 3 && t <= 6);
}

TEST("estimateTokens: any non-whitespace content yields at least 1 token") {
    ASSERT_TRUE(estimateTokens("x") >= (size_t)1);
}

// --- Step 2: backend selection (ICMG_TOKENIZER) -------------------------------
#include "../../src/core/bpe_tokenizer.hpp"
using icmg::core::TokBackend;
using icmg::core::tokBackendFromEnv;
using icmg::core::countTokensWith;
using icmg::core::BpeTokenizer;

TEST("tokBackendFromEnv: null/unknown -> Heuristic") {
    ASSERT_TRUE(tokBackendFromEnv(nullptr) == TokBackend::Heuristic);
    ASSERT_TRUE(tokBackendFromEnv("") == TokBackend::Heuristic);
    ASSERT_TRUE(tokBackendFromEnv("garbage") == TokBackend::Heuristic);
    ASSERT_TRUE(tokBackendFromEnv("heuristic") == TokBackend::Heuristic);
}

TEST("tokBackendFromEnv: recognises bpe + api values") {
    ASSERT_TRUE(tokBackendFromEnv("bpe-cl100k") == TokBackend::BpeCl100k);
    ASSERT_TRUE(tokBackendFromEnv("bpe-o200k") == TokBackend::BpeO200k);
    ASSERT_TRUE(tokBackendFromEnv("anthropic-api") == TokBackend::AnthropicApi);
}

TEST("countTokensWith: Heuristic == estimateTokens") {
    std::string s = "the quick brown fox jumps";
    ASSERT_EQ(countTokensWith(s, TokBackend::Heuristic, nullptr), estimateTokens(s));
}

TEST("countTokensWith: bpe backend with no vocab falls back to heuristic") {
    std::string s = "fallback when vocab missing";
    ASSERT_EQ(countTokensWith(s, TokBackend::BpeCl100k, nullptr), estimateTokens(s));
}

TEST("countTokensWith: AnthropicApi (unwired) falls back to heuristic") {
    std::string s = "api not wired yet";
    ASSERT_EQ(countTokensWith(s, TokBackend::AnthropicApi, nullptr), estimateTokens(s));
}

TEST("countTokensWith: ready bpe backend is used over heuristic") {
    BpeTokenizer bpe;
    bpe.addRankForTest("he", 0);   // make "he" merge so a known chunk has a known count
    // "hello" pretok -> one chunk "hello"; with only "he" rank: [he][l][l][o] = 4 tokens.
    // estimateTokens("hello") (5 letters /4 -> ceil = 2) differs, proving bpe path is used.
    size_t viaBpe = countTokensWith("hello", TokBackend::BpeCl100k, &bpe);
    ASSERT_EQ(viaBpe, (size_t)4);
}
