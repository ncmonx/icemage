// 2026-06-07: pure helpers for `icmg store --quick`.
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

// 2026-08-25: `--topic T` compat. Old AGENTS.md templates taught
// `icmg store --topic decisions-x "text"`; positional parsing stored
// topic="--topic", content="decisions-x" and SILENTLY DISCARDED the text
// (real data loss reported from the Kas Sementara project). resolveStoreArgs
// must honor the flag form.

TEST("quick_store: resolveStoreArgs positional (canonical form)") {
    auto r = resolveStoreArgs({"decisions-db", "use WAL mode"});
    ASSERT_EQ(r.topic, std::string("decisions-db"));
    ASSERT_EQ(r.content, std::string("use WAL mode"));
}

TEST("quick_store: resolveStoreArgs --topic compat form keeps content") {
    auto r = resolveStoreArgs({"--topic", "decisions-db", "use WAL mode"});
    ASSERT_EQ(r.topic, std::string("decisions-db"));
    ASSERT_EQ(r.content, std::string("use WAL mode"));
}

TEST("quick_store: resolveStoreArgs --topic after content") {
    auto r = resolveStoreArgs({"use WAL mode", "--topic", "decisions-db"});
    ASSERT_EQ(r.topic, std::string("decisions-db"));
    ASSERT_EQ(r.content, std::string("use WAL mode"));
}

TEST("quick_store: resolveStoreArgs --topic with other flags") {
    auto r = resolveStoreArgs({"--topic", "bug:x", "crash on start", "--kw", "a,b"});
    ASSERT_EQ(r.topic, std::string("bug:x"));
    ASSERT_EQ(r.content, std::string("crash on start"));
}

TEST("quick_store: resolveStoreArgs --topic missing value -> empty topic") {
    auto r = resolveStoreArgs({"--topic"});
    ASSERT_EQ(r.topic, std::string(""));
    ASSERT_EQ(r.content, std::string(""));
}
