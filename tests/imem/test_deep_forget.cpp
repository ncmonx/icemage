// TDD (2026-09-07): token-killer A -- deep-forget residue scan.
#include "../test_main.hpp"
#include "../../src/imem/deep_forget.hpp"

using namespace icmg::imem;

static ResidueCandidate cand(int64_t id, const std::string& src, const std::string& text) {
    return {id, src, text};
}

TEST("deep_forget: verbatim leak found in snapshot") {
    std::string secret = "database password is hunter2 for server alpha";
    std::vector<ResidueCandidate> v = {
        cand(10, "session-snapshot x", "Recent queries: database password is hunter2 for server alpha and more"),
        cand(11, "log-saved y", "Goal: fix build\nDecisions: use ninja"),
    };
    auto hits = findResidue(secret, v);
    ASSERT_EQ((int)hits.size(), 1);
    ASSERT_EQ(hits[0].id, (int64_t)10);
    ASSERT_TRUE(hits[0].overlap > 0.9);
}

TEST("deep_forget: partial paraphrase leak scored by containment not jaccard") {
    // Long artifact containing most of the forgotten tokens plus much else --
    // symmetric jaccard would be low, containment stays high.
    std::string secret = "api key sk_live_abcdef stored in config";
    std::string longtext = "Session log: many things happened today. The api key "
                           "sk_live_abcdef was stored in config by mistake, then we "
                           "talked about builds, tests, releases, and more unrelated "
                           "words repeated over and over again for padding purposes.";
    auto hits = findResidue(secret, {cand(5, "session-snapshot z", longtext)});
    ASSERT_EQ((int)hits.size(), 1);
    ASSERT_TRUE(hits[0].overlap >= 0.8);
}

TEST("deep_forget: unrelated artifacts not flagged") {
    std::string secret = "database password is hunter2 for server alpha";
    auto hits = findResidue(secret, {
        cand(1, "log-saved a", "Goal: ship v2 release\nDecisions: bump version"),
        cand(2, "quick:123", "vulkan shader hang, kill process tree"),
    });
    ASSERT_EQ((int)hits.size(), 0);
}

TEST("deep_forget: tiny forgotten text refuses to attribute") {
    auto hits = findResidue("ok", {cand(1, "x", "ok sure fine")});
    ASSERT_EQ((int)hits.size(), 0);
}

TEST("deep_forget: strongest leak first, capped") {
    std::string secret = "one two three four five six seven eight";
    std::vector<ResidueCandidate> v;
    v.push_back(cand(1, "half", "one two three four unrelated filler words here"));
    v.push_back(cand(2, "full", "one two three four five six seven eight verbatim"));
    auto hits = findResidue(secret, v, 0.4, 1);
    ASSERT_EQ((int)hits.size(), 1);
    ASSERT_EQ(hits[0].id, (int64_t)2);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
