// Pins the transcript -> (prompt, response) extractor that auto-record uses.
#include "../test_main.hpp"
#include "../../src/core/transcript_extract.hpp"
#include <string>
using namespace icmg::core;

TEST("transcript: extracts last user->assistant pair (string content)") {
    std::string jsonl =
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"how do I build\"}}\n"
        "{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":\"run build.ps1\"}}\n";
    auto p = extractLastPair(jsonl);
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.prompt, std::string("how do I build"));
    ASSERT_EQ(p.response, std::string("run build.ps1"));
}

TEST("transcript: array content concatenates text parts, skips tool_use") {
    std::string jsonl =
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"hi there\"}]}}\n"
        "{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":["
        "{\"type\":\"text\",\"text\":\"hello\"},{\"type\":\"tool_use\",\"name\":\"x\"}]}}\n";
    auto p = extractLastPair(jsonl);
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.prompt, std::string("hi there"));
    ASSERT_EQ(p.response, std::string("hello"));
}

TEST("transcript: tool_result user turns are skipped as prompts") {
    std::string jsonl =
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"real question\"}}\n"
        "{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":["
        "{\"type\":\"tool_use\",\"name\":\"Bash\"}]}}\n"
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":["
        "{\"type\":\"tool_result\",\"content\":\"output\"}]}}\n"
        "{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":\"the answer\"}}\n";
    auto p = extractLastPair(jsonl);
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.prompt, std::string("real question"));   // not the tool_result turn
    ASSERT_EQ(p.response, std::string("the answer"));
}

TEST("transcript: returns the LAST exchange when several exist") {
    std::string jsonl =
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"first\"}}\n"
        "{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":\"one\"}}\n"
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"second\"}}\n"
        "{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":\"two\"}}\n";
    auto p = extractLastPair(jsonl);
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.prompt, std::string("second"));
    ASSERT_EQ(p.response, std::string("two"));
}

TEST("transcript: malformed lines are skipped, not fatal") {
    std::string jsonl =
        "not json at all\n"
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"q\"}}\n"
        "{broken json\n"
        "{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":\"a\"}}\n";
    auto p = extractLastPair(jsonl);
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.prompt, std::string("q"));
    ASSERT_EQ(p.response, std::string("a"));
}

TEST("transcript: empty input yields ok=false") {
    auto p = extractLastPair("");
    ASSERT_TRUE(!p.ok);
}
