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


// v1.48.0 B2: sanitize user/assistant content before ChatML insertion.
// If a user types literally "<|im_end|>" the model would terminate its turn
// early — interpret special tokens only at structural positions, not inside
// content. Replace markers with safe sentinel; also strip ASCII control
// chars except newline/tab to avoid tokenizer edge cases.
inline std::string escapeChatMLContent(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    auto replaceAll = [&out](const std::string& needle, const std::string& repl,
                              std::string& haystack) {
        std::string::size_type pos = 0;
        while ((pos = haystack.find(needle, pos)) != std::string::npos) {
            haystack.replace(pos, needle.size(), repl);
            pos += repl.size();
        }
    };
    out = in;
    replaceAll("<|im_start|>", "< |im_start| >", out);
    replaceAll("<|im_end|>",   "< |im_end| >",   out);
    // Strip ASCII control chars (except \n=0x0A, \t=0x09, \r=0x0D).
    std::string cleaned;
    cleaned.reserve(out.size());
    for (char c : out) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 && u != 0x0A && u != 0x09 && u != 0x0D) continue;
        cleaned += c;
    }
    return cleaned;
}

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
    out += escapeChatMLContent(user);
    out += "<|im_end|>\n<|im_start|>assistant\n";
    return out;
}

// Stop token paired with the prompt builder above. Caller assigns to
// InferParams::stop.
inline const char* chatMLStopToken() { return "<|im_end|>"; }

// v1.48.0 B3: trim chat history to fit token budget. Estimate ~4 chars
// per token (conservative). Drop oldest pairs (user+assistant) until
// total content fits. Returns trimmed history (pass-through if fits).
// Default budget 8000 chars (~2000 tokens) leaves wide headroom under
// n_batch=8192. Safe for Indonesian / multi-byte unicode (~3 chars/tok).
inline std::vector<std::pair<std::string, std::string>>
trimChatHistory(
        const std::vector<std::pair<std::string, std::string>>& history,
        std::size_t max_chars = 16000) {
    std::size_t total = 0;
    for (const auto& [r, c] : history) total += r.size() + c.size() + 16;
    if (total <= max_chars) return history;
    auto out = history;
    // Drop pairs from front (oldest). Each pair = 2 entries (user+asst).
    while (total > max_chars && out.size() >= 2) {
        std::size_t drop = out[0].first.size() + out[0].second.size() + 16
                         + out[1].first.size() + out[1].second.size() + 16;
        out.erase(out.begin(), out.begin() + 2);
        total -= drop;
    }
    return out;
}

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
        out += escapeChatMLContent(content);
        out += "<|im_end|>\n";
    }
    out += "<|im_start|>user\n";
    out += escapeChatMLContent(current_user);
    out += "<|im_end|>\n<|im_start|>assistant\n";
    return out;
}


} // namespace icmg::llm
