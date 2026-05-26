// v1.47.0: ChatML prompt builder.
//
// Wraps a system + user message in ChatML format so local LLMs (Qwen 2.5
// family by default; Phi-3.5 / Llama-3.1 accept via special-token parsing)
// know where each role's turn ends. Without wrapping, the model continues
// in autocomplete mode and fabricates fake "User:" lines, looping forever.
//
// Stop token: <|im_end|> — caller sets InferParams::stop to ensure the
// generation halts at the end of the assistant turn instead of running
// over into a hallucinated follow-up.
#pragma once

#include <string>
#include <vector>
#include <utility>

namespace icmg::llm {

// Returns prompt formatted as:
//   <|im_start|>system\n{system}<|im_end|>\n
//   <|im_start|>user\n{user}<|im_end|>\n
//   <|im_start|>assistant\n
//
// When system is empty, the system turn is omitted. The trailing "assistant\n"
// is the priming marker for generation — the model continues from there.
inline std::string buildChatMLPrompt(const std::string& system,
                                     const std::string& user) {
    std::string out;
    if (!system.empty()) {
        out  = "<|im_start|>system\n";
        out += system;
        out += "<|im_end|>\n";
    }
    out += "<|im_start|>user\n";
    out += user;
    out += "<|im_end|>\n<|im_start|>assistant\n";
    return out;
}

// Stop token paired with the prompt builder above. Caller assigns to
// InferParams::stop.
inline const char* chatMLStopToken() { return "<|im_end|>"; }

// v1.47.0 multi-turn: build ChatML with full conversation history.
// `history` is pairs of (role, content) where role is "user" or
// "assistant". Caller appends each user msg + assistant reply.
// Output ends with primer "<|im_start|>assistant\n" — model
// continues from there. Fixes the "halo halo every turn" bug
// where each turn was sent as cold-start with no prior context.
inline std::string buildChatMLPromptMulti(
        const std::string& system,
        const std::vector<std::pair<std::string, std::string>>& history,
        const std::string& current_user) {
    std::string out;
    if (!system.empty()) {
        out  = "<|im_start|>system\n";
        out += system;
        out += "<|im_end|>\n";
    }
    for (const auto& [role, content] : history) {
        out += "<|im_start|>";
        out += role;
        out += "\n";
        out += content;
        out += "<|im_end|>\n";
    }
    out += "<|im_start|>user\n";
    out += current_user;
    out += "<|im_end|>\n<|im_start|>assistant\n";
    return out;
}


} // namespace icmg::llm
