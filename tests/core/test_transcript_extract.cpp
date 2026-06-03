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

TEST("strip: a pure stop-hook-reminder message strips to empty") {
    std::string s = stripInjectedContext(
        "<stop-hook-reminders>\nSession summary: 1085min\nDecision log: ...\n</stop-hook-reminders>");
    ASSERT_TRUE(s.empty());
}

TEST("strip: marker lines (icmg/MODE/Persona) are removed") {
    std::string s = stripInjectedContext(
        "build the auth module\n[icmg] relevant command: icmg suggest\nMODE: long-session test");
    ASSERT_EQ(s, std::string("build the auth module"));
}

TEST("strip: a clean human prompt is left untouched") {
    std::string s = stripInjectedContext("how do I rebuild the binary");
    ASSERT_EQ(s, std::string("how do I rebuild the binary"));
}

TEST("strip: a real wrapped Stop-hook message strips to empty") {
    // Real format: a 'Stop hook feedback:' line + a <stop-hook-reminders> block.
    std::string s = stripInjectedContext(
        "Stop hook feedback:\n<stop-hook-reminders>\nSession summary: 1000min\nDecision log: x\n</stop-hook-reminders>");
    ASSERT_TRUE(s.empty());
}

TEST("strip: human prompt survives even with a trailing hook block") {
    std::string s = stripInjectedContext(
        "fix the auth bug please\n<stop-hook-reminders>\nSession summary\n</stop-hook-reminders>");
    ASSERT_EQ(s, std::string("fix the auth bug please"));
}

TEST("transcript: hook-noise user turn is NOT captured as a prompt") {
    // last real user prompt is "real question"; the later user turn is a stop-hook
    // reminder which must be ignored so the captured prompt is the real one.
    std::string jsonl =
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"real question here\"}}\n"
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"<stop-hook-reminders>\\nSession summary\\n</stop-hook-reminders>\"}}\n"
        "{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":\"the answer\"}}\n";
    auto p = extractLastPair(jsonl);
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.prompt, std::string("real question here"));
    ASSERT_EQ(p.response, std::string("the answer"));
}
