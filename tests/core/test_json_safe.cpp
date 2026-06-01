// v1.68.1: safeDump must never throw on invalid UTF-8 (the augmented-prompt
// abort bug: icmg memory content held raw bytes -> json::dump type_error.316
// -> uncaught -> abort).

#include "../test_main.hpp"
#include "../../src/core/json_safe.hpp"

#include <string>

using icmg::core::safeDump;
using nlohmann::json;

TEST("safeDump: invalid UTF-8 content does not throw") {
    // 0x89 is the leading byte of a PNG header — a lone invalid UTF-8 byte.
    std::string bad;
    bad.push_back((char)0x89);
    bad += "PNG raw bytes";
    json j;
    j["content"] = bad;
    bool threw = false;
    std::string out;
    try {
        out = safeDump(j);
    } catch (...) {
        threw = true;
    }
    ASSERT_FALSE(threw);
    ASSERT_FALSE(out.empty());
    // U+FFFD replacement char (UTF-8 EF BF BD) appears for the bad byte.
    ASSERT_CONTAINS(out, "\xEF\xBF\xBD");
}

TEST("safeDump: raw default dump DOES throw (confirms the bug + need for fix)") {
    std::string bad;
    bad.push_back((char)0x89);
    json j;
    j["content"] = bad;
    bool threw = false;
    try {
        (void)j.dump();   // default handler = throw on invalid UTF-8
    } catch (...) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

TEST("safeDump: valid content round-trips unchanged") {
    json j;
    j["a"] = "hello";
    j["n"] = 42;
    std::string out = safeDump(j);
    ASSERT_CONTAINS(out, "hello");
    ASSERT_CONTAINS(out, "42");
    // parses back cleanly
    json back = json::parse(out);
    ASSERT_EQ(back["a"].get<std::string>(), std::string("hello"));
}
