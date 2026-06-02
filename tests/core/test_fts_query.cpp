// v2.0.0 search snapshot: build a safe FTS5 MATCH expression from arbitrary user
// input (tokens -> prefix terms, implicit AND), injection-proof (no FTS5 operators
// leak through). Pure; the FTS5 virtual table + snapshot builder are wired on top.

#include "../test_main.hpp"
#include "../../src/core/fts_query.hpp"

#include <string>

using namespace icmg::core;

TEST("ftsQuery: single identifier -> prefix term") {
    ASSERT_EQ(ftsQuery("GraphStore"), std::string("GraphStore*"));
}

TEST("ftsQuery: multi-word -> AND of prefix terms") {
    ASSERT_EQ(ftsQuery("resolve edges"), std::string("resolve* edges*"));
}

TEST("ftsQuery: punctuation split into tokens") {
    // a-b.c -> three tokens
    ASSERT_EQ(ftsQuery("a_path.cpp"), std::string("a_path* cpp*"));
}

TEST("ftsQuery: strips FTS operators / quotes (injection-proof)") {
    auto q = ftsQuery("foo\" OR bar -baz");
    // no raw quote, no bare OR/-, only sanitized prefix terms
    ASSERT_NOT_CONTAINS(q, std::string("\""));
    ASSERT_CONTAINS(q, std::string("foo*"));
    ASSERT_CONTAINS(q, std::string("bar*"));
    ASSERT_CONTAINS(q, std::string("baz*"));
}

TEST("ftsQuery: drops sub-2-char noise tokens") {
    ASSERT_EQ(ftsQuery("a bb ccc"), std::string("bb* ccc*"));
}

TEST("ftsQuery: empty / pure-punctuation -> empty") {
    ASSERT_EQ(ftsQuery(""), std::string(""));
    ASSERT_EQ(ftsQuery("  -- ()"), std::string(""));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
