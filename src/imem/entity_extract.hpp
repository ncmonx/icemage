// 2026-06-07: rule-based entity extraction (luna idea: enrich Layer-0, zero-LLM). Pure, no IO.
// Pulls URLs / IPv4 / env-vars / @mentions from free text via regex so auto-captured memory
// is searchable by the entities it references. Returns "type:value" tokens (keyword-ready).
#pragma once
#include <string>
#include <vector>
#include <regex>
#include <algorithm>

namespace icmg::imem {

// Extract entities as "type:value" tokens, de-duplicated, capped (avoid keyword bloat).
// Types: url, ip, env (-> $VAR or %VAR%), mention (-> @name).
inline std::vector<std::string> extractEntities(const std::string& text, size_t cap = 12) {
    std::vector<std::string> out;
    auto add = [&](const std::string& tok){
        if (out.size() >= cap) return;
        if (std::find(out.begin(), out.end(), tok) == out.end()) out.push_back(tok);
    };
    auto scan = [&](const std::regex& re, const std::string& type, int grp){
        for (auto it = std::sregex_iterator(text.begin(), text.end(), re);
             it != std::sregex_iterator(); ++it) {
            std::string v = (*it)[grp].str();
            if (!v.empty()) add(type + ":" + v);
        }
    };
    // URL first (so its host isn't also caught as a bare IP/mention).
    scan(std::regex(R"((https?://[^\s\)\]\"'<>]+))"), "url", 1);
    scan(std::regex(R"(\b(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})\b)"), "ip", 1);
    // env: $VAR or %VAR% -> name only (group 1 or 2, whichever matched).
    {
        std::regex re(R"(\$([A-Za-z_][A-Za-z0-9_]*)|%([A-Za-z_][A-Za-z0-9_]*)%)");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), re);
             it != std::sregex_iterator(); ++it) {
            std::string name = (*it)[1].matched ? (*it)[1].str() : (*it)[2].str();
            if (!name.empty()) add("env:" + name);
        }
    }
    scan(std::regex(R"((?:^|[\s(\[])@([A-Za-z0-9_]+))"), "mention", 1);
    return out;
}

} // namespace icmg::imem
