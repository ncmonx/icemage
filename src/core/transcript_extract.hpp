#pragma once
// Best-effort extraction of the last (user prompt -> assistant response) exchange
// from a Claude Code transcript JSONL. Used by `icmg prompt-capture` (a Stop hook)
// to auto-record real Q/A into prompt_history so a future similar prompt can reuse
// the past answer -- no manual qa-add. Defensive: never throws; returns ok=false on
// anything it cannot parse, so a hook is never broken by a transcript format change.
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace icmg::core {

struct QAPair { std::string prompt, response; bool ok = false; };

// Concatenate the text of a CC message "content" (string, or array of typed parts).
// Skips non-text parts (tool_use/tool_result/image). Returns "" if no text.
inline std::string ccContentText(const nlohmann::json& content) {
    if (content.is_string()) return content.get<std::string>();
    std::string out;
    if (content.is_array()) {
        for (const auto& part : content) {
            if (part.is_object() && part.value("type", "") == "text" && part.contains("text") &&
                part["text"].is_string()) {
                if (!out.empty()) out += "\n";
                out += part["text"].get<std::string>();
            }
        }
    }
    return out;
}

// True if a user message carries a tool_result (an automated turn, not a real prompt).
inline bool ccHasToolResult(const nlohmann::json& content) {
    if (!content.is_array()) return false;
    for (const auto& part : content)
        if (part.is_object() && part.value("type", "") == "tool_result") return true;
    return false;
}

// Scan the JSONL and return the last assistant text response paired with the most
// recent real (non-tool-result, non-empty) user prompt preceding it.
inline QAPair extractLastPair(const std::string& jsonl) {
    QAPair result;
    std::string pendingUser;
    std::istringstream in(jsonl);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        nlohmann::json j;
        try { j = nlohmann::json::parse(line); } catch (...) { continue; }
        if (!j.is_object()) continue;
        const std::string type = j.value("type", "");
        if (!j.contains("message") || !j["message"].is_object()) continue;
        const auto& msg = j["message"];
        if (!msg.contains("content")) continue;
        const auto& content = msg["content"];

        if (type == "user") {
            if (ccHasToolResult(content)) continue;        // automated tool turn -> skip
            std::string t = ccContentText(content);
            if (!t.empty()) pendingUser = t;               // remember latest real prompt
        } else if (type == "assistant") {
            std::string t = ccContentText(content);
            if (!t.empty() && !pendingUser.empty()) {      // complete the exchange
                result.prompt = pendingUser;
                result.response = t;
                result.ok = true;
            }
        }
    }
    return result;
}

}  // namespace icmg::core
