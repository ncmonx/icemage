// v1.47.0 TDD: ChatML prompt builder.
//
// Pure-function unit tests — no DB, no LLM load. Covers:
//   - System+user wrap shape (3 turns: system, user, assistant priming).
//   - Empty system → 2 turns (user + assistant), no leading system block.
//   - Special characters in user input preserved verbatim.
//   - Stop token marker matches Qwen 2.5 ChatML end-of-turn marker.
#include "../test_main.hpp"
#include "../../src/llm/chat_template.hpp"

#include <string>

namespace llm = icmg::llm;

TEST("chat_template: system+user wraps in 3-turn ChatML") {
    std::string p = llm::buildChatMLPrompt("you are claudy", "halo");
    // System turn present
    ASSERT_TRUE(p.find("<|im_start|>system\nyou are claudy<|im_end|>\n")
                != std::string::npos);
    // User turn present
    ASSERT_TRUE(p.find("<|im_start|>user\nhalo<|im_end|>\n")
                != std::string::npos);
    // Assistant priming at tail (generation continues from there)
    ASSERT_TRUE(p.size() >= std::string("<|im_start|>assistant\n").size());
    ASSERT_EQ(p.substr(p.size() - std::string("<|im_start|>assistant\n").size()),
              std::string("<|im_start|>assistant\n"));
}

TEST("chat_template: empty system omits system turn") {
    std::string p = llm::buildChatMLPrompt("", "just say hi");
    // No system marker leaked
    ASSERT_TRUE(p.find("<|im_start|>system") == std::string::npos);
    // User+assistant still wired
    ASSERT_TRUE(p.find("<|im_start|>user\njust say hi<|im_end|>\n")
                != std::string::npos);
    ASSERT_TRUE(p.find("<|im_start|>assistant\n") != std::string::npos);
}

TEST("chat_template: user content preserved verbatim incl. special chars") {
    const std::string tricky = "line 1\nline 2\t\"quoted\" & < > | $vars";
    std::string p = llm::buildChatMLPrompt("sys", tricky);
    ASSERT_TRUE(p.find(tricky) != std::string::npos);
}

TEST("chat_template: stop token is the ChatML end-of-turn marker") {
    ASSERT_EQ(std::string(llm::chatMLStopToken()), std::string("<|im_end|>"));
}

TEST("chat_template: empty user still produces valid wrap (no crash)") {
    std::string p = llm::buildChatMLPrompt("sys", "");
    // user turn block opens then immediately closes
    ASSERT_TRUE(p.find("<|im_start|>user\n<|im_end|>\n") != std::string::npos);
    // assistant priming still appended
    ASSERT_TRUE(p.find("<|im_start|>assistant\n") != std::string::npos);
}


TEST("chat_template: multi-turn empty history matches single-turn builder") {
    std::vector<std::pair<std::string,std::string>> empty;
    std::string a = llm::buildChatMLPromptMulti("sys", empty, "hi");
    std::string b = llm::buildChatMLPrompt("sys", "hi");
    ASSERT_EQ(a, b);
}

TEST("chat_template: multi-turn includes prior turns verbatim") {
    std::vector<std::pair<std::string,std::string>> hist = {
        {"user", "halo"},
        {"assistant", "halo juga"},
        {"user", "kabar?"},
        {"assistant", "baik"},
    };
    std::string p = llm::buildChatMLPromptMulti("sys", hist, "lagi ngapain?");
    // All 4 prior turns present
    ASSERT_TRUE(p.find("<|im_start|>user\nhalo<|im_end|>") != std::string::npos);
    ASSERT_TRUE(p.find("<|im_start|>assistant\nhalo juga<|im_end|>") != std::string::npos);
    ASSERT_TRUE(p.find("<|im_start|>user\nkabar?<|im_end|>") != std::string::npos);
    ASSERT_TRUE(p.find("<|im_start|>assistant\nbaik<|im_end|>") != std::string::npos);
    // Current user turn last + assistant primer
    ASSERT_TRUE(p.find("<|im_start|>user\nlagi ngapain?<|im_end|>") != std::string::npos);
    ASSERT_TRUE(p.substr(p.size() - std::string("<|im_start|>assistant\n").size())
                == "<|im_start|>assistant\n");
}

TEST("chat_template: multi-turn empty system + history works") {
    std::vector<std::pair<std::string,std::string>> hist = {{"user", "a"}, {"assistant", "b"}};
    std::string p = llm::buildChatMLPromptMulti("", hist, "c");
    ASSERT_TRUE(p.find("<|im_start|>system") == std::string::npos);
    ASSERT_TRUE(p.find("<|im_start|>user\na<|im_end|>") != std::string::npos);
    ASSERT_TRUE(p.find("<|im_start|>assistant\nb<|im_end|>") != std::string::npos);
    ASSERT_TRUE(p.find("<|im_start|>user\nc<|im_end|>") != std::string::npos);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
