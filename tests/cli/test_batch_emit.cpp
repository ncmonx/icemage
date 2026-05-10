#include "../test_main.hpp"
#include "../../src/cli/batch_builder.hpp"

using icmg::cli::buildBatchSpec;
using icmg::cli::BatchOpts;
using icmg::cli::BatchDirective;

TEST("batch: 3 tasks → 3 requests with custom_id task-N") {
    auto j = buildBatchSpec({"a", "b", "c"});
    ASSERT_EQ((int)j["requests"].size(), 3);
    ASSERT_EQ(j["requests"][0]["custom_id"].get<std::string>(), std::string("task-1"));
    ASSERT_EQ(j["requests"][1]["custom_id"].get<std::string>(), std::string("task-2"));
    ASSERT_EQ(j["requests"][2]["custom_id"].get<std::string>(), std::string("task-3"));
}

TEST("batch: default model + max_tokens") {
    auto j = buildBatchSpec({"x"});
    auto& p = j["requests"][0]["params"];
    ASSERT_EQ(p["model"].get<std::string>(), std::string("claude-sonnet-4-5"));
    ASSERT_EQ(p["max_tokens"].get<int>(), 2000);
}

TEST("batch: custom model + max_tokens") {
    BatchOpts o; o.model = "claude-haiku-4-5"; o.max_tokens = 500;
    auto j = buildBatchSpec({"x"}, o);
    auto& p = j["requests"][0]["params"];
    ASSERT_EQ(p["model"].get<std::string>(), std::string("claude-haiku-4-5"));
    ASSERT_EQ(p["max_tokens"].get<int>(), 500);
}

TEST("batch: no-think directive prepended") {
    BatchOpts o; o.directive = BatchDirective::NoThink;
    auto j = buildBatchSpec({"do X"}, o);
    std::string content = j["requests"][0]["params"]["messages"][0]["content"];
    ASSERT_TRUE(content.find("Answer directly without analysis") != std::string::npos);
    ASSERT_TRUE(content.find("do X") != std::string::npos);
}

TEST("batch: concise directive") {
    BatchOpts o; o.directive = BatchDirective::Concise;
    auto j = buildBatchSpec({"y"}, o);
    std::string content = j["requests"][0]["params"]["messages"][0]["content"];
    ASSERT_TRUE(content.find("under 100 words") != std::string::npos);
}

TEST("batch: caveman directive") {
    BatchOpts o; o.directive = BatchDirective::Caveman;
    auto j = buildBatchSpec({"z"}, o);
    std::string content = j["requests"][0]["params"]["messages"][0]["content"];
    ASSERT_TRUE(content.find("Caveman mode ultra") != std::string::npos);
}

TEST("batch: no directive = no preamble") {
    auto j = buildBatchSpec({"plain"});
    std::string content = j["requests"][0]["params"]["messages"][0]["content"];
    ASSERT_EQ(content, std::string("plain"));
}

TEST("batch: custom id prefix") {
    BatchOpts o; o.id_prefix = "job";
    auto j = buildBatchSpec({"a"}, o);
    ASSERT_EQ(j["requests"][0]["custom_id"].get<std::string>(), std::string("job-1"));
}

TEST("batch: empty tasks → empty requests array") {
    auto j = buildBatchSpec({});
    ASSERT_EQ((int)j["requests"].size(), 0);
}

// Phase 72: Anthropic Batch API schema shape — top-level requests + each
// request has custom_id (string), params object, params has model + max_tokens
// + messages array, messages[0] has role + content.
TEST("batch: schema shape conforms to Anthropic Batch API contract") {
    auto j = buildBatchSpec({"hello"});
    ASSERT_TRUE(j.contains("requests"));
    ASSERT_TRUE(j["requests"].is_array());
    auto& r0 = j["requests"][0];
    ASSERT_TRUE(r0.contains("custom_id"));
    ASSERT_TRUE(r0["custom_id"].is_string());
    ASSERT_TRUE(r0.contains("params"));
    auto& p = r0["params"];
    ASSERT_TRUE(p.contains("model"));
    ASSERT_TRUE(p["model"].is_string());
    ASSERT_TRUE(p.contains("max_tokens"));
    ASSERT_TRUE(p["max_tokens"].is_number_integer());
    ASSERT_TRUE(p.contains("messages"));
    ASSERT_TRUE(p["messages"].is_array());
    ASSERT_EQ((int)p["messages"].size(), 1);
    auto& m0 = p["messages"][0];
    ASSERT_EQ(m0["role"].get<std::string>(), std::string("user"));
    ASSERT_TRUE(m0["content"].is_string());
}

TEST("batch: large batch (50 tasks) — id increments correctly") {
    std::vector<std::string> tasks;
    for (int i = 0; i < 50; ++i) tasks.push_back("task body " + std::to_string(i));
    auto j = buildBatchSpec(tasks);
    ASSERT_EQ((int)j["requests"].size(), 50);
    ASSERT_EQ(j["requests"][0]["custom_id"].get<std::string>(), std::string("task-1"));
    ASSERT_EQ(j["requests"][49]["custom_id"].get<std::string>(), std::string("task-50"));
}

TEST("batch: special chars in task — JSON escape preserved") {
    std::string tricky = "line1\nline2 \"quoted\" \\backslash\\";
    auto j = buildBatchSpec({tricky});
    std::string content = j["requests"][0]["params"]["messages"][0]["content"];
    ASSERT_EQ(content, tricky);  // round-trip via json
    // Serialized JSON must be valid (parse-able).
    std::string dumped = j.dump();
    auto reparsed = nlohmann::json::parse(dumped);
    ASSERT_EQ(reparsed["requests"][0]["params"]["messages"][0]["content"].get<std::string>(),
              tricky);
}

TEST("batch: long task body — pass-through, no truncation") {
    std::string big(20000, 'x');
    auto j = buildBatchSpec({big});
    std::string content = j["requests"][0]["params"]["messages"][0]["content"];
    ASSERT_EQ((int)content.size(), 20000);
}

TEST("batch: caveman directive prefixes task content (not replaces)") {
    BatchOpts o; o.directive = BatchDirective::Caveman;
    auto j = buildBatchSpec({"refactor auth.cs"}, o);
    std::string content = j["requests"][0]["params"]["messages"][0]["content"];
    // Caveman preamble must precede task body.
    auto cav_pos  = content.find("Caveman mode");
    auto task_pos = content.find("refactor auth.cs");
    ASSERT_TRUE(cav_pos != std::string::npos);
    ASSERT_TRUE(task_pos != std::string::npos);
    ASSERT_TRUE(cav_pos < task_pos);
}

int main() {
    std::cout << "=== batch emit tests ===\n";
    return icmg::test::run_all();
}
