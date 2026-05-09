// Phase 65 T2: `icmg lint-claudemd` — drift detection.
//
// Scans CLAUDE.md (and AGENTS.md) for file paths and symbol names; verifies
// each exists in the graph. Stale instructions waste context tokens AND
// mislead Claude into wrong assumptions.
//
// Heuristics:
//   - File path: ` `path/to/file.ext` ` backticked tokens with `/` and known ext
//   - Symbol: backticked CamelCase or snake_case identifiers (≥4 chars)
// Reports each as OK / MISSING / RENAMED suggestion.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../graph/graph_store.hpp"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace icmg::cli {

class LintClaudeMdCommand : public BaseCommand {
public:
    std::string name()        const override { return "lint-claudemd"; }
    std::string description() const override {
        return "Detect stale file paths / symbol refs in CLAUDE.md / AGENTS.md";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg lint-claudemd [path]\n\n"
            "Default checks both CLAUDE.md and AGENTS.md in cwd. Reports\n"
            "backticked file paths missing from the graph and symbol refs\n"
            "with no matching graph_node.\n\n"
            "Options:\n"
            "  --json         Machine-readable output\n"
            "  --quiet        Only print summary line\n"
            "  --strict       Exit 1 when any drift found (CI gate)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool json_out = hasFlag(args, "--json");
        bool quiet    = hasFlag(args, "--quiet");
        bool strict   = hasFlag(args, "--strict");

        // Find target files
        std::vector<fs::path> targets;
        std::string explicit_path;
        for (auto& a : args) if (!a.empty() && a[0] != '-') { explicit_path = a; break; }
        if (!explicit_path.empty()) {
            if (fs::exists(explicit_path)) targets.push_back(explicit_path);
        } else {
            for (auto* f : {"CLAUDE.md", "AGENTS.md"}) {
                fs::path p = fs::current_path() / f;
                if (fs::exists(p)) targets.push_back(p);
            }
        }
        if (targets.empty()) {
            std::cerr << "icmg lint-claudemd: no CLAUDE.md / AGENTS.md found in cwd.\n";
            return 1;
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        // Backticked tokens
        std::regex tick_re(R"(`([^`\n]{2,80})`)");
        std::regex path_hint(R"([\\/].+\.(cs|ts|tsx|js|jsx|py|rb|go|rs|cpp|hpp|c|h|java|kt|sql|md|json|yaml|yml|toml|ini|sh|bat|ps1))",
                             std::regex::icase);
        std::regex symbol_hint(R"(^[A-Za-z_][A-Za-z0-9_]{3,}$)");
        std::regex flag_hint(R"(^--[a-z0-9][a-z0-9-]+$)");
        std::regex shell_hint(R"(^(icmg|git|npm|node|python3?|cargo|cmake|gh|sh|bash|sudo)\b)");

        struct Hit { std::string token; std::string kind; std::string status; };
        std::vector<Hit> hits;
        std::set<std::string> seen;

        int ok_count = 0, miss_count = 0;

        for (auto& target : targets) {
            std::ifstream f(target);
            std::stringstream buf; buf << f.rdbuf();
            std::string text = buf.str();
            for (auto it = std::sregex_iterator(text.begin(), text.end(), tick_re);
                 it != std::sregex_iterator(); ++it) {
                std::string tok = (*it)[1].str();
                if (tok.empty() || seen.count(tok)) continue;
                seen.insert(tok);
                // Skip flags + shell commands + tokens with spaces
                if (std::regex_search(tok, flag_hint)) continue;
                if (std::regex_search(tok, shell_hint)) continue;
                if (tok.find(' ') != std::string::npos) continue;

                Hit h; h.token = tok;
                if (std::regex_search(tok, path_hint)) {
                    h.kind = "path";
                    auto node = store.getNode(tok);
                    if (node) { h.status = "ok"; ++ok_count; }
                    else      { h.status = "missing"; ++miss_count; }
                } else if (std::regex_match(tok, symbol_hint)) {
                    h.kind = "symbol";
                    auto syms = store.findSymbol(tok);
                    if (!syms.empty()) { h.status = "ok"; ++ok_count; }
                    else               { h.status = "missing"; ++miss_count; }
                } else {
                    continue;  // not a path or symbol candidate
                }
                hits.push_back(h);
            }
        }

        if (json_out) {
            std::cout << "{\"checked\":" << (ok_count + miss_count)
                      << ",\"ok\":" << ok_count << ",\"missing\":" << miss_count
                      << ",\"hits\":[";
            for (size_t i = 0; i < hits.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "{\"token\":\"" << hits[i].token
                          << "\",\"kind\":\"" << hits[i].kind
                          << "\",\"status\":\"" << hits[i].status << "\"}";
            }
            std::cout << "]}\n";
        } else if (quiet) {
            std::cout << "lint-claudemd: " << ok_count << " ok, "
                      << miss_count << " stale\n";
        } else {
            std::cout << "icmg lint-claudemd:\n";
            for (auto& h : hits) {
                if (h.status == "missing") {
                    std::cout << "  ! " << h.kind << " missing: `" << h.token << "`\n";
                }
            }
            std::cout << "\n";
            std::cout << "Summary: " << ok_count << " refs verified, "
                      << miss_count << " stale.\n";
            if (miss_count > 0) {
                std::cout << "  Update CLAUDE.md or run `icmg graph scan` if graph is stale.\n";
            }
        }

        if (strict && miss_count > 0) return 1;
        return 0;
    }
};

ICMG_REGISTER_COMMAND("lint-claudemd", LintClaudeMdCommand);

} // namespace icmg::cli
