// BPE byte-pair-merge core (tiktoken-style). The merge algorithm is the testable
// heart: given mergeable ranks, repeatedly merge the lowest-rank adjacent pair
// until none remain; the token count = number of surviving pieces. Tested with a
// tiny synthetic rank table (no 1.7MB vocab needed to prove the algorithm).
#include "../test_main.hpp"
#include "../../src/core/bpe_tokenizer.hpp"

using icmg::core::BpeTokenizer;

// Build a tokenizer from an in-memory synthetic rank table.
static BpeTokenizer synth(std::initializer_list<std::pair<std::string,int>> ranks) {
    BpeTokenizer t;
    for (auto& r : ranks) t.addRankForTest(r.first, r.second);
    return t;
}

TEST("bpe: no merges -> one token per byte") {
    auto t = synth({});                       // empty rank table: nothing merges
    ASSERT_EQ(t.mergeCount("abc"), (size_t)3);
}

TEST("bpe: single pair merge reduces count by one") {
    auto t = synth({{"ab", 0}});              // only "ab" mergeable
    ASSERT_EQ(t.mergeCount("abc"), (size_t)2);  // [ab][c]
}

TEST("bpe: greedy lowest-rank merge chains to a full token") {
    // a+b->ab (rank 0), ab+c->abc (rank 2). Lowest rank merges first, then chains.
    auto t = synth({{"ab", 0}, {"bc", 1}, {"abc", 2}});
    ASSERT_EQ(t.mergeCount("abc"), (size_t)1);  // [abc]
}

TEST("bpe: lowest rank wins when pairs compete") {
    // "bc" (rank 0) should merge before "ab" (rank 5); then "a"+"bc" has no rank.
    auto t = synth({{"ab", 5}, {"bc", 0}});
    ASSERT_EQ(t.mergeCount("abc"), (size_t)2);  // [a][bc]
}

TEST("bpe: empty piece -> 0 tokens") {
    auto t = synth({{"ab", 0}});
    ASSERT_EQ(t.mergeCount(""), (size_t)0);
}

TEST("bpe: single byte -> 1 token") {
    auto t = synth({});
    ASSERT_EQ(t.mergeCount("x"), (size_t)1);
}

TEST("bpe: not-ready tokenizer reports not ready") {
    BpeTokenizer t;
    ASSERT_TRUE(!t.ready());                   // no ranks loaded
}

// --- Step 3/4: cl100k pre-tokenizer splits (proven vs known tiktoken output) ---
#include <vector>
static std::vector<std::string> PT(const std::string& s) { return BpeTokenizer::preTokenize(s); }

TEST("pretok: words split with leading space glued") {
    auto v = PT("hello world");
    ASSERT_EQ(v.size(), (size_t)2);
    ASSERT_EQ(v[0], std::string("hello"));
    ASSERT_EQ(v[1], std::string(" world"));
}

TEST("pretok: contraction splits off (don't -> don, 't)") {
    auto v = PT("don't");
    ASSERT_EQ(v.size(), (size_t)2);
    ASSERT_EQ(v[0], std::string("don"));
    ASSERT_EQ(v[1], std::string("'t"));
}

TEST("pretok: I'm happy -> I, 'm, ' happy'") {
    auto v = PT("I'm happy");
    ASSERT_EQ(v.size(), (size_t)3);
    ASSERT_EQ(v[0], std::string("I"));
    ASSERT_EQ(v[1], std::string("'m"));
    ASSERT_EQ(v[2], std::string(" happy"));
}

TEST("pretok: letters and digits split apart") {
    auto v = PT("a1b2");
    ASSERT_EQ(v.size(), (size_t)4);
    ASSERT_EQ(v[0], std::string("a"));
    ASSERT_EQ(v[1], std::string("1"));
    ASSERT_EQ(v[2], std::string("b"));
    ASSERT_EQ(v[3], std::string("2"));
}

TEST("pretok: digits group in runs of at most 3") {
    auto v = PT("123456");
    ASSERT_EQ(v.size(), (size_t)2);
    ASSERT_EQ(v[0], std::string("123"));
    ASSERT_EQ(v[1], std::string("456"));
}

TEST("pretok: double space -> single + space-glued word") {
    auto v = PT("  hello");
    ASSERT_EQ(v.size(), (size_t)2);
    ASSERT_EQ(v[0], std::string(" "));
    ASSERT_EQ(v[1], std::string(" hello"));
}

TEST("pretok: trailing newline is its own chunk") {
    auto v = PT("hello\n");
    ASSERT_EQ(v.size(), (size_t)2);
    ASSERT_EQ(v[0], std::string("hello"));
    ASSERT_EQ(v[1], std::string("\n"));
}

TEST("pretok: punctuation alone, then space-glued word") {
    auto v = PT("foo, bar");
    ASSERT_EQ(v.size(), (size_t)3);
    ASSERT_EQ(v[0], std::string("foo"));
    ASSERT_EQ(v[1], std::string(","));
    ASSERT_EQ(v[2], std::string(" bar"));
}
