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
