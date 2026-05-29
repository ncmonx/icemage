// v1.70.0 (#176): recall --json must emit valid UTF-8 even when memory content
// holds raw non-UTF-8 bytes, so downstream json::dump() never throws.

#include "../test_main.hpp"
#include "../../src/cli/recall_json.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using icmg::cli::recallNodesToJson;
using nlohmann::json;

TEST("recall-json: invalid UTF-8 content -> still parseable JSON") {
    icmg::imem::MemoryNode n;
    n.id = 7;
    n.topic = "decisions-db";
    n.content = std::string("note ") + (char)0x89 + "PNG raw";   // invalid UTF-8
    n.keywords = "db";
    n.importance = 2;
    n.frequency = 3;
    n.score = 1.25;
    n.git_sha = "abc123";

    std::string out = recallNodesToJson({n});
    bool threw = false;
    json parsed;
    try { parsed = json::parse(out); } catch (...) { threw = true; }
    ASSERT_FALSE(threw);                       // valid UTF-8 -> parses cleanly
    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.size(), (size_t)1);
    ASSERT_EQ(parsed[0]["id"].get<long long>(), (long long)7);
    ASSERT_EQ(parsed[0]["topic"].get<std::string>(), std::string("decisions-db"));
    // re-dumping must not throw either (the original downstream crash)
    bool redump_threw = false;
    try { (void)parsed.dump(); } catch (...) { redump_threw = true; }
    ASSERT_FALSE(redump_threw);
}

TEST("recall-json: clean content round-trips fields") {
    icmg::imem::MemoryNode n;
    n.id = 1; n.topic = "t"; n.content = "hello"; n.keywords = "k";
    n.importance = 1; n.frequency = 1; n.score = 0.5; n.git_sha = "";
    json parsed = json::parse(recallNodesToJson({n}));
    ASSERT_EQ(parsed[0]["content"].get<std::string>(), std::string("hello"));
}

TEST("recall-json: empty node list -> empty array") {
    std::string out = recallNodesToJson({});
    json parsed = json::parse(out);
    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.size(), (size_t)0);
}
