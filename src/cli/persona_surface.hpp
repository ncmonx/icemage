// Persona-rule surfacing: which persona-DB entries may be injected per-prompt,
// and how to format them for a UserPromptSubmit hook.
//
// Cross-project behavioral rules live in the dedicated zone `_rules`. ONLY that
// zone surfaces on a prompt match — identity/feeling/passphrase/etc. stay
// private and are never injected per-prompt. Pure + header-only so it is
// trivially testable; the command layer does the DB search + JSON emit.
#pragma once

#include <string>
#include <vector>
#include <cctype>

namespace icmg::cli {

// The one zone whose entries are surfaced per-prompt across projects.
inline constexpr const char* kRuleZone = "_rules";

// May this persona zone be injected on a per-prompt match?
inline bool shouldSurfacePersonaZone(const std::string& zone) {
    return zone == kRuleZone;
}

inline std::string toLowerCopy(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// Does the prompt mention any keyword of this rule? Direction matters: we ask
// "does the PROMPT contain a rule keyword" (so a natural sentence like "aku mau
// makan" fires the makan-rule), NOT "does the rule contain every prompt word".
// A prompt word (>=3 chars) that appears as a substring of the rule content is
// a match. Conservative + cheap.
inline bool ruleMatchesPrompt(const std::string& ruleContent, const std::string& prompt) {
    const std::string rc = toLowerCopy(ruleContent);
    const std::string p  = toLowerCopy(prompt);
    std::string word;
    auto check = [&]() -> bool {
        const bool hit = (word.size() >= 3 && rc.find(word) != std::string::npos);
        word.clear();
        return hit;
    };
    for (char c : p) {
        if (std::isalnum(static_cast<unsigned char>(c))) word.push_back(c);
        else if (check()) return true;
    }
    return check();
}

// Build the additionalContext payload from matched rule contents.
// Returns "" when there is nothing to surface. Caps to keep prompts lean.
inline std::string buildSurfaceContext(const std::vector<std::string>& rules,
                                       size_t cap = 3) {
    if (rules.empty()) return "";
    std::string out = "[persona-rule] active cross-project rules for this prompt:";
    size_t n = 0;
    for (const auto& r : rules) {
        if (n++ >= cap) break;
        out += "\n- " + r;
    }
    return out;
}

} // namespace icmg::cli
