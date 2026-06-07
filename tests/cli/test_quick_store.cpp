// 2026-06-07: pure helpers for `icmg store --quick` (#luna-batch).
#include "../test_main.hpp"
#include "../../src/cli/quick_store_helpers.hpp"

using namespace icmg::cli;

TEST("quick_store: quickTopic is deterministic + prefixed") {
    ASSERT_EQ(quickTopic(1780799399), std::string("quick:1780799399"));
    ASSERT_EQ(quickTopic(0), std::string("quick:0"));
}

TEST("quick_store: firstPositional skips flags + flag values") {
    std::vector<std::string> vf = {"--kw","--importance","--ttl","--source"};
    // store --quick "msg"
    ASSERT_EQ(firstPositional({"--quick","hello world"}, vf), std::string("hello world"));
    // store "msg" --quick
    ASSERT_EQ(firstPositional({"hello","--quick"}, vf), std::string("hello"));
    // store --kw foo --quick "msg"  -> "foo" is a flag value, skip it
    ASSERT_EQ(firstPositional({"--kw","foo","--quick","msg"}, vf), std::string("msg"));
    // no positional
    ASSERT_EQ(firstPositional({"--quick"}, vf), std::string(""));
}
