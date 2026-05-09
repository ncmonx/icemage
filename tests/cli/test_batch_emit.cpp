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

int main() {
    std::cout << "=== batch emit tests ===\n";
    return icmg::test::run_all();
}
