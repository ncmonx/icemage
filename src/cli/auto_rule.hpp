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

    auto trim_token = [](std::string s) -> std::string {
        while (!s.empty() && (s.back() == '.' || s.back() == ',' ||
                              s.back() == '!' || s.back() == '?' ||
                              s.back() == ' ' || s.back() == '\t'))
            s.pop_back();
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        return s.substr(i);
    };

    struct PrefixAction { const char* prefix; NLAction action; };
    static const PrefixAction remove_rule_prefixes[] = {
        {"hapus rule ", NLAction::REMOVE_RULE},
        {"buang rule ", NLAction::REMOVE_RULE},
        {"ilangin rule ", NLAction::REMOVE_RULE},
        {"remove rule ", NLAction::REMOVE_RULE},
        {"delete rule ", NLAction::REMOVE_RULE},
        {"drop rule ", NLAction::REMOVE_RULE},
    };
    static const PrefixAction edit_rule_prefixes[] = {
        {"ubah rule ", NLAction::EDIT_RULE},
        {"ganti rule ", NLAction::EDIT_RULE},
        {"update rule ", NLAction::EDIT_RULE},
        {"edit rule ", NLAction::EDIT_RULE},
        {"change rule ", NLAction::EDIT_RULE},
    };

    auto match_prefix = [&](const PrefixAction* arr, size_t n) -> const PrefixAction* {
        for (size_t i = 0; i < n; ++i)
            if (lc.rfind(arr[i].prefix, 0) == 0) return &arr[i];
        return nullptr;
    };

    if (auto p = match_prefix(remove_rule_prefixes,
                              sizeof(remove_rule_prefixes)/sizeof(remove_rule_prefixes[0]))) {
        r.action = p->action;
        r.target_name = trim_token(line.substr(std::string(p->prefix).size()));
        if (r.target_name.empty()) r.action = NLAction::NONE;
        return r;
    }
    if (auto p = match_prefix(edit_rule_prefixes,
                              sizeof(edit_rule_prefixes)/sizeof(edit_rule_prefixes[0]))) {
        std::string rest = line.substr(std::string(p->prefix).size());
        static const char* seps[] = { " jadi ", " to ", " => " };
        size_t cut = std::string::npos; size_t cut_len = 0;
        for (const auto* s : seps) {
            auto pos = rest.find(s);
            if (pos != std::string::npos && (cut == std::string::npos || pos < cut)) {
                cut = pos; cut_len = std::string(s).size();
            }
        }
        if (cut == std::string::npos) return r;  // NONE — parse fail
        r.action = p->action;
        r.target_name = trim_token(rest.substr(0, cut));
        r.content = trim_token(rest.substr(cut + cut_len));
        if (r.target_name.empty() || r.content.empty()) r.action = NLAction::NONE;
        return r;
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
