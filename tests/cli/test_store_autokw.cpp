// TDD (2026-06-15): auto-keyword derivation on store (semantic-title v2).
// Spec: claude-mem idea — when the user stores a memory without --kw, derive a
// compact set of salient keywords from the content so recall/BM25 has signal.
// Pure-function layer so the extraction is unit-testable without a DB.
// Failing FIRST: src/cli/store_autokw.hpp does not exist yet.

#include "../test_main.hpp"
#include "../../src/cli/store_autokw.hpp"

#include <string>

using icmg::cli::autoKeywords;

// 1. Picks salient content words, drops common stopwords, comma-joined.
TEST("store-autokw: extracts salient words, drops stopwords") {
    std::string kw = autoKeywords(
        "the parser was failing on the windows path because of a quoting bug", 5);
    ASSERT_CONTAINS(kw, "parser");
    ASSERT_CONTAINS(kw, "windows");
    ASSERT_NOT_CONTAINS(kw, "the");
    ASSERT_NOT_CONTAINS(kw, "was");
    ASSERT_NOT_CONTAINS(kw, "of");
}

// 2. Caps at the requested max count (comma-separated tokens <= max).
TEST("store-autokw: caps at max keywords") {
    std::string kw = autoKeywords(
        "alpha bravo charlie delta echo foxtrot golf hotel india juliet", 3);
    int commas = 0; for (char c : kw) if (c == ',') ++commas;
    ASSERT_TRUE(commas <= 2);   // <=3 tokens => <=2 commas
}

// 3. Dedups repeated words (case-insensitive), keeps one.
TEST("store-autokw: dedups repeated words case-insensitively") {
    std::string kw = autoKeywords("Cache cache CACHE invalidation invalidation", 5);
    // "cache" should appear once
    size_t f = kw.find("cache");
    ASSERT_TRUE(f != std::string::npos);
    ASSERT_TRUE(kw.find("cache", f + 1) == std::string::npos);
}

// 4. Empty / stopword-only content -> empty string (no crash).
TEST("store-autokw: empty or stopword-only content yields empty") {
    ASSERT_EQ(autoKeywords("", 5), std::string(""));
    ASSERT_EQ(autoKeywords("the a of and to in is", 5), std::string(""));
}

// 5. Lowercases output + strips punctuation around tokens.
TEST("store-autokw: lowercases and strips punctuation") {
    std::string kw = autoKeywords("Refactored GraphStore::resolveAndInsertEdges()!", 5);
    ASSERT_NOT_CONTAINS(kw, "!");
    ASSERT_NOT_CONTAINS(kw, "(");
    // token retained in lowercased form
    ASSERT_CONTAINS(kw, "refactored");
}
