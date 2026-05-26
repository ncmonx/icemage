#pragma once
#include <string>
#include <vector>
#include <functional>

namespace icmg::cli {

enum class NLAction {
    NONE, ADD_RULE, REMOVE_RULE, EDIT_RULE,
    ADD_SKILL, REMOVE_SKILL
};

struct NLDetectResult {
    NLAction action = NLAction::NONE;
    std::string target_name;
    std::string content;
};

inline NLDetectResult detectNL(const std::string& line) {
    NLDetectResult r;
    if (line.size() < 8 || line.size() > 500) return r;

    // Lowercase first 80 chars for prefix-match window.
    std::string lc;
    lc.reserve(80);
    for (size_t i = 0; i < line.size() && i < 80; ++i) {
        char c = line[i];
        lc += (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
    }

    static const char* add_rule_triggers[] = {
        "ingat ya", "tolong ingat", "aturannya ", "aturan baru",
        "jangan pernah ", "selalu ", "harus selalu", "mulai sekarang",
        "sejak sekarang", "peraturan baru:",
        "remember ", "from now on", "please always", "please never",
        "always ", "never ", "rule:",
    };
    for (const auto* t : add_rule_triggers) {
        if (lc.rfind(t, 0) == 0) {
            r.action = NLAction::ADD_RULE;
            r.content = line;
            return r;
        }
    }
    return r;
}

} // namespace icmg::cli
