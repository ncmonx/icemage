#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <cmath>

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
    if (std::getenv("ICMG_NO_AUTO_RULE")) return r;
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

    static const PrefixAction add_skill_prefixes[] = {
        {"tambah skill ", NLAction::ADD_SKILL},
        {"buat skill ",   NLAction::ADD_SKILL},
        {"add skill ",    NLAction::ADD_SKILL},
        {"create skill ", NLAction::ADD_SKILL},
    };
    static const PrefixAction remove_skill_prefixes[] = {
        {"hapus skill ",  NLAction::REMOVE_SKILL},
        {"buang skill ",  NLAction::REMOVE_SKILL},
        {"remove skill ", NLAction::REMOVE_SKILL},
        {"delete skill ", NLAction::REMOVE_SKILL},
    };

    if (auto p = match_prefix(remove_skill_prefixes,
                              sizeof(remove_skill_prefixes)/sizeof(remove_skill_prefixes[0]))) {
        r.action = p->action;
        r.target_name = trim_token(line.substr(std::string(p->prefix).size()));
        if (r.target_name.empty()) r.action = NLAction::NONE;
        return r;
    }
    if (auto p = match_prefix(add_skill_prefixes,
                              sizeof(add_skill_prefixes)/sizeof(add_skill_prefixes[0]))) {
        std::string rest = line.substr(std::string(p->prefix).size());
        size_t sp = rest.find(' ');
        if (sp == std::string::npos) return r;  // NONE — name only
        r.action = p->action;
        r.target_name = trim_token(rest.substr(0, sp));
        r.content = trim_token(rest.substr(sp + 1));
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


struct RuleRecord {
    std::string id;
    std::string name;
    std::string content;
};
struct FuzzyMatch {
    std::string id;
    std::string name;
    double score = 0.0;
};

inline std::vector<std::string> tokenize_lower(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        char lc = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        if ((lc >= 'a' && lc <= 'z') || (lc >= '0' && lc <= '9')) cur += lc;
        else if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

inline std::vector<FuzzyMatch> fuzzyFind(
    const std::string& query,
    const std::vector<RuleRecord>& corpus,
    double threshold = 0.5)
{
    auto qtoks = tokenize_lower(query);
    if (qtoks.empty() || corpus.empty()) return {};

    std::vector<FuzzyMatch> out;
    for (const auto& r : corpus) {
        auto name_toks = tokenize_lower(r.name);
        auto body_toks = tokenize_lower(r.content);
        double score = 0.0;
        for (const auto& qt : qtoks) {
            for (const auto& nt : name_toks) {
                if (nt == qt) score += 3.0;
                else if (nt.find(qt) != std::string::npos) score += 1.5;
            }
            for (const auto& bt : body_toks) {
                if (bt == qt) score += 1.0;
            }
        }
        double denom = name_toks.empty() ? 1.0 : std::sqrt((double)name_toks.size());
        score /= denom;
        score = std::min(1.0, score / 3.0);
        if (score >= threshold) out.push_back({r.id, r.name, score});
    }
    std::sort(out.begin(), out.end(),
              [](const FuzzyMatch& a, const FuzzyMatch& b){ return a.score > b.score; });
    return out;
}

using RuleSaver    = std::function<int(const std::string& name, const std::string& body, bool update)>;
using RuleDisabler = std::function<int(const std::string& id)>;
using RuleLister   = std::function<std::vector<RuleRecord>()>;
using SkillSaver   = std::function<int(const std::string& name, const std::string& body, bool update)>;
using SkillRemover = std::function<int(const std::string& name)>;
using SkillLister  = std::function<std::vector<RuleRecord>()>;

struct NLAdapters {
    RuleSaver    rule_save;
    RuleDisabler rule_disable;
    RuleLister   rule_list;
    SkillSaver   skill_save;
    SkillRemover skill_remove;
    SkillLister  skill_list;
};

// v1.53.0 Sub-D: list ambiguous candidates for interactive disambig in REPL.
// Caller invokes when handleNL returns string containing "ambiguous". Returns
// up to top-5 fuzzy matches (top-1 if score >= 0.9 ranked above any tied tail).
inline std::vector<FuzzyMatch> listAmbiguous(
    const std::string& query,
    const std::vector<RuleRecord>& corpus,
    double threshold = 0.5,
    size_t limit = 5)
{
    auto m = fuzzyFind(query, corpus, threshold);
    if (m.size() > limit) m.resize(limit);
    return m;
}

inline std::string handleNL(const std::string& line, const NLAdapters& a) {
    auto d = detectNL(line);
    if (d.action == NLAction::NONE) return "";

    auto resolve = [&](const std::vector<RuleRecord>& corpus) -> std::string {
        auto m = fuzzyFind(d.target_name, corpus, 0.5);
        if (m.empty()) return "";
        if (m.size() >= 2 && (m[0].score - m[1].score) < 0.05) return "__AMBIGUOUS__";
        return m.front().id;
    };

    switch (d.action) {
    case NLAction::ADD_RULE: {
        if (!a.rule_save) return "";
        std::string name = "chat-auto-" + std::to_string(std::time(nullptr));
        int rc = a.rule_save(name, d.content, false);
        if (rc == 0) return "(auto-rule saved: " + name + " - \\unrule to remove)";
        return "(rule save failed)";
    }
    case NLAction::REMOVE_RULE: {
        if (!a.rule_list || !a.rule_disable) return "";
        auto corpus = a.rule_list();
        auto id = resolve(corpus);
        if (id.empty()) return "(no rule matching \"" + d.target_name + "\" - \\rules to list)";
        if (id == "__AMBIGUOUS__") return "(ambiguous match - use \\unrule with ID)";
        int rc = a.rule_disable(id);
        if (rc == 0) return "(rule disabled: " + d.target_name + " - reversible via icmg rule enable)";
        return "(rule disable failed)";
    }
    case NLAction::EDIT_RULE: {
        if (!a.rule_list || !a.rule_save) return "";
        auto corpus = a.rule_list();
        auto id = resolve(corpus);
        if (id.empty()) return "(no rule matching \"" + d.target_name + "\" - \\rules to list)";
        if (id == "__AMBIGUOUS__") return "(ambiguous match - use \\rule with ID)";
        std::string name;
        for (const auto& r : corpus) if (r.id == id) { name = r.name; break; }
        int rc = a.rule_save(name, d.content, true);
        if (rc == 0) return "(rule updated: " + name + ")";
        return "(rule update failed)";
    }
    case NLAction::ADD_SKILL: {
        if (!a.skill_save) return "";
        int rc = a.skill_save(d.target_name, d.content, false);
        if (rc == 0) return "(skill saved: " + d.target_name + ")";
        return "(skill save failed)";
    }
    case NLAction::REMOVE_SKILL: {
        if (!a.skill_list || !a.skill_remove) return "";
        auto corpus = a.skill_list();
        auto id = resolve(corpus);
        if (id.empty()) return "(no skill matching \"" + d.target_name + "\" - icmg skill list)";
        if (id == "__AMBIGUOUS__") return "(ambiguous skill match)";
        std::string name;
        for (const auto& r : corpus) if (r.id == id) { name = r.name; break; }
        int rc = a.skill_remove(name);
        if (rc == 0) return "(skill removed: " + name + ")";
        return "(skill remove failed)";
    }
    default: return "";
    }
}

} // namespace icmg::cli
