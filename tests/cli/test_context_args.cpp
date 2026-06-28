// `icmg context` arg-parsing tests (2026-06-28).
// Regression guard: value-taking flags (--head/--tail/--lines/...) must consume
// their N value, NOT have it mistaken for a second file token. Born from the
// v2.11.2 --head bug where `context f --head 5` treated "5" as a file.
#include "../test_main.hpp"
#include "../../src/cli/context_batch.hpp"

using namespace icmg::cli;

TEST("context-args: --head value not treated as file") {
    auto files = collectContextFiles({"foo.cpp", "--head", "5"});
    ASSERT_EQ((int)files.size(), 1);
    ASSERT_EQ(files[0], std::string("foo.cpp"));
}

TEST("context-args: --tail value not treated as file") {
    auto files = collectContextFiles({"foo.cpp", "--tail", "3"});
    ASSERT_EQ((int)files.size(), 1);
    ASSERT_EQ(files[0], std::string("foo.cpp"));
}

TEST("context-args: --lines value not treated as file") {
    auto files = collectContextFiles({"foo.cpp", "--lines", "10-20"});
    ASSERT_EQ((int)files.size(), 1);
}

TEST("context-args: genuine two-file batch still detected") {
    auto files = collectContextFiles({"a.cpp", "b.hpp"});
    ASSERT_EQ((int)files.size(), 2);
}

TEST("context-args: two files with head flag -> still two files") {
    auto files = collectContextFiles({"a.cpp", "b.hpp", "--head", "5"});
    ASSERT_EQ((int)files.size(), 2);
}

TEST("context-args: singleFileArgs keeps head flag + value") {
    auto sub = singleFileArgs({"a.cpp", "b.hpp", "--head", "5"}, "a.cpp");
    // expect: a.cpp --head 5  (b.hpp dropped, flag+value retained)
    bool has_a = false, has_head = false, has_5 = false, has_b = false;
    for (auto& s : sub) {
        if (s == "a.cpp") has_a = true;
        if (s == "--head") has_head = true;
        if (s == "5") has_5 = true;
        if (s == "b.hpp") has_b = true;
    }
    ASSERT_TRUE(has_a);
    ASSERT_TRUE(has_head);
    ASSERT_TRUE(has_5);
    ASSERT_FALSE(has_b);
}
