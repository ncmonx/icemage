// Phase 28 T1: `icmg lint-style` — text-pattern style/UI consistency lint.
//
// Two modes:
//   1. Diff-style: lint NEW file against REF file's structural markers
//      (default rule set: bracket-style buttons, naming, event-handlers).
//   2. Custom rules: load .icmg/style.json and apply must_match/must_not_match
//      regex per file glob.
//
// Exit code = warning count. CI gate friendly.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <vector>
#include <set>
#include <filesystem>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

struct StyleWarning {
    std::string file;
    int         line;
    std::string rule;
    std::string msg;
    StyleWarning(std::string f, int l, std::string r, std::string m)
        : file(std::move(f)), line(l), rule(std::move(r)), msg(std::move(m)) {}
};

class LintStyleCommand : public BaseCommand {
public:
    std::string name()        const override { return "lint-style"; }
    std::string description() const override { return "Text-pattern style/UI consistency lint"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg lint-style <file> [--ref <ref-file>] [--rules <json>] [--all] [--json]\n\n"
            "Modes:\n"
            "  --ref X         Diff-style lint against X's structural markers\n"
            "  --rules X.json  Custom rules JSON (must_match / must_not_match per glob)\n"
            "  --all           Batch over current dir matching --ref pattern\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        std::string ref       = flagValue(args, "--ref");
        std::string rules_pat = flagValue(args, "--rules");
        bool batch_all        = hasFlag(args, "--all");
        bool json_out         = hasFlag(args, "--json");

        std::string target;
        for (auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            target = a; break;
        }
        if (target.empty() && !batch_all) {
            std::cerr << "lint-style: <file> required (or --all)\n";
            return 1;
        }

        // Load custom rules if provided.
        std::vector<json> custom;
        if (!rules_pat.empty()) {
            std::ifstream f(rules_pat);
            if (!f) { std::cerr << "lint-style: cannot read rules: " << rules_pat << "\n"; return 1; }
            std::ostringstream s; s << f.rdbuf();
            try {
                auto j = json::parse(s.str());
                if (j.contains("rules") && j["rules"].is_array()) {
                    for (auto& r : j["rules"]) custom.push_back(r);
                }
            } catch (const std::exception& e) {
                std::cerr << "lint-style: invalid rules json: " << e.what() << "\n";
                return 1;
            }
        }

        std::vector<StyleWarning> warnings;
        std::vector<std::string> targets;
        if (batch_all) {
            // Walk CWD.
            for (auto& e : fs::recursive_directory_iterator(".")) {
                if (!e.is_regular_file()) continue;
                targets.push_back(e.path().string());
            }
        } else {
            targets.push_back(target);
        }

        for (auto& f : targets) {
            std::ifstream in(f);
            if (!in) continue;
            std::ostringstream s; s << in.rdbuf();
            std::string content = s.str();

            // Custom rules.
            for (auto& r : custom) {
                std::string in_files = r.value("in_files", "");
                if (!in_files.empty()) {
                    try {
                        std::regex fre(in_files);
                        if (!std::regex_search(f, fre)) continue;
                    } catch (...) { continue; }
                }
                std::string nm = r.value("name", "rule");
                std::string mm = r.value("must_match", "");
                std::string mn = r.value("must_not_match", "");
                if (!mm.empty()) {
                    try {
                        if (!std::regex_search(content, std::regex(mm))) {
                            warnings.emplace_back(f, 0, nm, "must_match not satisfied: " + mm);
                        }
                    } catch (...) {}
                }
                if (!mn.empty()) {
                    try {
                        std::smatch m;
                        if (std::regex_search(content, m, std::regex(mn))) {
                            int line = 1;
                            for (size_t i = 0; i < (size_t)m.position(0); ++i)
                                if (content[i] == '\n') ++line;
                            warnings.emplace_back(f, line, nm, "forbidden pattern: " + mn);
                        }
                    } catch (...) {}
                }
            }

            // Diff-style vs ref.
            if (!ref.empty()) {
                applyDiffStyle(content, f, ref, warnings);
            }
        }

        if (json_out) {
            std::cout << "[";
            for (size_t i = 0; i < warnings.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "{\"file\":\"" << escJson(warnings[i].file)
                          << "\",\"line\":" << warnings[i].line
                          << ",\"rule\":\"" << escJson(warnings[i].rule)
                          << "\",\"msg\":\"" << escJson(warnings[i].msg) << "\"}";
            }
            std::cout << "]\n";
        } else {
            for (auto& w : warnings) {
                std::cout << w.file << ":" << w.line << ": [" << w.rule << "] " << w.msg << "\n";
            }
            std::cout << warnings.size() << " warning(s)\n";
        }
        return (int)warnings.size();
    }

private:
    void applyDiffStyle(const std::string& content, const std::string& path,
                         const std::string& ref, std::vector<StyleWarning>& out) {
        std::ifstream rf(ref);
        if (!rf) return;
        std::ostringstream s; s << rf.rdbuf();
        std::string ref_content = s.str();

        // Rule 1: if ref has `Style="{...}"` on Button, new should too.
        std::regex re_styled_button(R"(<Button[^>]*Style="\{)");
        if (std::regex_search(ref_content, re_styled_button)) {
            std::regex re_bare(R"(<Button(?![^>]*Style=))");
            std::smatch m;
            std::string c = content;
            int offset = 0;
            while (std::regex_search(c, m, re_bare)) {
                int line = 1;
                for (size_t i = 0; i < (size_t)(offset + m.position(0)); ++i) {
                    if (path.size() && i < content.size() && content[i] == '\n') ++line;
                }
                out.emplace_back(path, line, "use-styled-button",
                               "ref uses Style=\"{...}\" on Button; new file has bare <Button>");
                offset += (int)m.position(0) + (int)m.length(0);
                c = c.substr(m.position(0) + m.length(0));
            }
        }

        // Rule 2: if ref imports a base class / module, new should too.
        std::regex re_using(R"(^\s*using\s+([\w\.]+);)");
        std::set<std::string> ref_usings;
        for (auto it = std::sregex_iterator(ref_content.begin(), ref_content.end(), re_using);
             it != std::sregex_iterator(); ++it) ref_usings.insert((*it)[1].str());
        std::set<std::string> new_usings;
        for (auto it = std::sregex_iterator(content.begin(), content.end(), re_using);
             it != std::sregex_iterator(); ++it) new_usings.insert((*it)[1].str());
        for (auto& u : ref_usings) {
            if (!new_usings.count(u)) {
                out.emplace_back(path, 0, "missing-using",
                               "ref has `using " + u + ";` not present in new");
            }
        }

        // Rule 3: event-handler naming consistency (On<Verb>Clicked).
        std::regex re_handler(R"(\bvoid\s+(\w+_Click)\b)");
        if (std::regex_search(ref_content, re_handler)) {
            // ref uses _Click style — flag On<...>Clicked in new.
            std::regex re_alt(R"(\bvoid\s+(On\w+Clicked)\b)");
            for (auto it = std::sregex_iterator(content.begin(), content.end(), re_alt);
                 it != std::sregex_iterator(); ++it) {
                int line = 1;
                for (size_t i = 0; i < (size_t)it->position(1); ++i)
                    if (content[i] == '\n') ++line;
                out.emplace_back(path, line, "handler-style-mismatch",
                               "ref uses `_Click`; new uses `OnXClicked`");
            }
        }
    }

    static std::string escJson(const std::string& s) {
        std::string out; out.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
            else if (c == '\n') out += "\\n";
            else if (c == '\t') out += "\\t";
            else out.push_back(c);
        }
        return out;
    }
};

ICMG_REGISTER_COMMAND("lint-style", LintStyleCommand);

} // namespace icmg::cli
