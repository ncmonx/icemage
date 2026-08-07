// tests/imem/test_contradiction_scan.cpp
// v2.21 research C: contradiction sentinel (pure heuristic scan).
#include "../test_main.hpp"
#include "../../src/imem/contradiction_scan.hpp"

using namespace icmg::imem;

static MemFact fact(int64_t id, const std::string& content, int64_t ts) {
    MemFact f; f.id = id; f.content = content; f.created_at = ts; return f;
}

TEST("contra: negated correction of same topic is detected, old->new") {
    std::vector<MemFact> facts = {
        fact(1, "project icemage build uses powershell 5.1 for build.ps1", 1000),
        fact(2, "project icemage build no longer uses powershell 5.1 for build.ps1", 2000),
    };
    auto c = findContradictionCandidates(facts);
    ASSERT_EQ(c.size(), (size_t)1);
    ASSERT_EQ(c[0].old_id, (int64_t)1);
    ASSERT_EQ(c[0].new_id, (int64_t)2);
    ASSERT_TRUE(c[0].reason.find("negation") != std::string::npos);
}

TEST("contra: conflicting key=value detected even without negation words") {
    std::vector<MemFact> facts = {
        fact(10, "icemage config: default timeout = 30 for scanner runs", 1000),
        fact(11, "icemage config: default timeout = 90 for scanner runs", 5000),
    };
    auto c = findContradictionCandidates(facts);
    ASSERT_EQ(c.size(), (size_t)1);
    ASSERT_EQ(c[0].old_id, (int64_t)10);
    ASSERT_EQ(c[0].new_id, (int64_t)11);
    ASSERT_TRUE(c[0].reason.find("timeout") != std::string::npos);
}

TEST("contra: unrelated topics are NOT flagged (low overlap)") {
    std::vector<MemFact> facts = {
        fact(1, "the parser handles sql ddl files with tree-sitter", 1000),
        fact(2, "release workflow must not skip the changelog update", 2000),
    };
    auto c = findContradictionCandidates(facts);
    ASSERT_EQ(c.size(), (size_t)0);
}

TEST("contra: near-duplicates without negation are NOT contradictions") {
    std::vector<MemFact> facts = {
        fact(1, "scanner walks the filesystem and feeds graph edges", 1000),
        fact(2, "the scanner walks the filesystem and feeds the graph edges", 2000),
    };
    auto c = findContradictionCandidates(facts);
    ASSERT_EQ(c.size(), (size_t)0);   // consolidate's job, not ours
}

TEST("contra: direction follows created_at regardless of input order") {
    std::vector<MemFact> facts = {
        fact(5, "vulkan backend is deprecated for the local llm embedder", 9000),  // newer first
        fact(3, "vulkan backend is used for the local llm embedder", 100),
    };
    auto c = findContradictionCandidates(facts);
    ASSERT_EQ(c.size(), (size_t)1);
    ASSERT_EQ(c[0].old_id, (int64_t)3);
    ASSERT_EQ(c[0].new_id, (int64_t)5);
}

TEST("contra: empty input -> empty output, no crash") {
    auto c = findContradictionCandidates({});
    ASSERT_EQ(c.size(), (size_t)0);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
