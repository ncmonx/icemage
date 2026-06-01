// v1.70.0 (#177): icmg llm list must emit pure JSON with an "active" key and
// no trailing plain-text line.

#include "../test_main.hpp"
#include "../../src/cli/llm_list_json.hpp"

#include <nlohmann/json.hpp>
#include <string>

using icmg::cli::buildLlmListJson;
using nlohmann::json;

TEST("llm-list-json: output is pure JSON (parses with no trailing content)") {
    std::string registry = R"({"schema_version":1,"default":"qwen2.5-0.5b-q4","models":[{"id":"qwen2.5-0.5b-q4"},{"id":"qwen2.5-1.5b-q4"}]})";
    std::string out = buildLlmListJson(registry, "qwen2.5-1.5b-q4");
    bool threw = false;
    json j;
    try { j = json::parse(out); } catch (...) { threw = true; }
    ASSERT_FALSE(threw);                       // whole output parses — no trailing text
    ASSERT_EQ(j["active"].get<std::string>(), std::string("qwen2.5-1.5b-q4"));
    ASSERT_EQ(j["default"].get<std::string>(), std::string("qwen2.5-0.5b-q4"));
    ASSERT_TRUE(j["models"].is_array());
    ASSERT_NOT_CONTAINS(out, "active:");        // no trailing plain-text line
}

TEST("llm-list-json: empty active -> active key present and empty") {
    std::string out = buildLlmListJson(R"({"models":[]})", "");
    json j = json::parse(out);
    ASSERT_EQ(j["active"].get<std::string>(), std::string(""));
}

TEST("llm-list-json: unparseable registry -> still valid JSON with active") {
    std::string out = buildLlmListJson("not json at all", "m1");
    json j = json::parse(out);                  // must not throw
    ASSERT_EQ(j["active"].get<std::string>(), std::string("m1"));
    ASSERT_TRUE(j["models"].is_array());
}
