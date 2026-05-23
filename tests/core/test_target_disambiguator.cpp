// v1.4.0 Task 1: target disambiguator unit tests.
// Tests the pure-stdlib trigram Jaccard + suffix bonus scoring.

#include "../test_main.hpp"
#include "../../src/core/target_disambiguator.hpp"

#include <string>
#include <vector>

using icmg::core::DisambigCandidate;
using icmg::core::disambiguateTargets;
using icmg::core::trigramJaccard;
using icmg::core::tokenize;

// ---- TEST 1: two candidates same suffix → both above threshold --------
//
// "foobar" and "bazbar" share 3-char suffix "bar".
// Prompt "foobar bazbar" → both tokens are exact matches to candidate names.
// trigramJaccard("foobar","foobar") = 1.0  → score 1.0 ≥ 0.80
// trigramJaccard("bazbar","bazbar") = 1.0  → score 1.0 ≥ 0.80
// → expect ≥2 results, all ≥ 0.80

TEST("disambig: two candidates same suffix above threshold") {
    std::vector<std::pair<std::string,std::string>> cands = {
        {"foobar", ""},
        {"bazbar", ""},
    };
    auto res = disambiguateTargets("foobar bazbar", cands, 0.80);
    ASSERT_TRUE(res.size() >= 2);
    for (auto& r : res) {
        ASSERT_TRUE(r.score >= 0.80);
    }
}

// ---- TEST 2: single clear match → exactly 1 result -------------------------
//
// Same candidates, prompt "foobar only".
// "foobar" token → trigramJaccard("foobar","foobar")=1.0 → score 1.0 ≥ 0.80
// Best match for "bazbar": trigramJaccard("foobar","bazbar") ≈ 0.14 + 0.10 bonus ≈ 0.24 < 0.80
// → exactly 1 result: "foobar"

TEST("disambig: single clear match returns 1 result") {
    std::vector<std::pair<std::string,std::string>> cands = {
        {"foobar", ""},
        {"bazbar", ""},
    };
    auto res = disambiguateTargets("foobar only", cands, 0.80);
    ASSERT_EQ((int)res.size(), 1);
    ASSERT_EQ(res[0].name, std::string("foobar"));
}

// ---- TEST 3: unrelated prompt → empty result --------------------------------

TEST("disambig: no matches on unrelated prompt") {
    std::vector<std::pair<std::string,std::string>> cands = {
        {"foobar", ""},
        {"bazbar", ""},
    };
    auto res = disambiguateTargets("deploy production", cands, 0.80);
    ASSERT_TRUE(res.empty());
}

// ---- TEST 4: high threshold filters fuzzy matches --------------------------
//
// With threshold 0.95, a score of 1.0 is >= 0.95 → results are NOT filtered out.
// Use a threshold so high nothing hits: 0.9999.

TEST("disambig: threshold 0.9999 filters all below-perfect matches") {
    std::vector<std::pair<std::string,std::string>> cands = {
        {"foobar", ""},
        {"bazbar", ""},
    };
    // Prompt only matches "foobar" exactly (score 1.0); "bazbar" ≈ 0.24 < 0.9999
    // But score 1.0 >= 0.9999 → "foobar" passes. To test filtering, use a
    // partial match prompt where NO candidate scores perfectly.
    // "foob baz" → best for foobar: trigramJaccard("foob","foobar")
    // "foob" trigrams: {"foo","oob"}, "foobar" trigrams: {"foo","oob","oba","bar"}
    // isect=2, union=6, jacc=0.333; with suffix bonus 0.10 → 0.433 < 0.9999
    auto res = disambiguateTargets("foob baz", cands, 0.9999);
    ASSERT_TRUE(res.empty());
}

// ---- TEST 5: trigramJaccard symmetric --------------------------------------

TEST("disambig: trigramJaccard symmetric") {
    double fwd = trigramJaccard("foo", "bar");
    double rev = trigramJaccard("bar", "foo");
    // Use integer comparison via scaled values to avoid floating-point exact eq issues.
    ASSERT_EQ((int)(fwd * 10000), (int)(rev * 10000));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
