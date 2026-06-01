// `icmg rules` — .icmgrules Memory Bank
//
// Manages project-level rule files (Cline-style .icmgrules/*.md).
// Each .md file in .icmgrules/ is one rule; auto-synced via `icmg init`.
// Active rules are injected at SessionStart via `icmg rules inject`.
//
// Subcommands:
//   list [--json]          Show all rules (path, tag, active, bytes)
//   add <file>             Upsert a single file into the bank
//   disable <path>         Set active=0 for rule at <path>
//   enable  <path>         Set active=1 for rule at <path>
//   sync                   Walk .icmgrules/ and upsert all *.md files
//   inject                 Emit active rules as markdown (used by SessionStart hook)
//
// .icmgignore note: minimal glob support — exact paths and simple *.ext patterns.
// For complex patterns (negations, double-stars) use .gitignore instead.
//
// Opt-out: ICMG_RULES_QUIET=1 → `inject` emits nothing.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/db.hpp"
#include "../../core/migrator.hpp"
#include "../../core/rules_store.hpp"
#include "../../core/project_context.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using icmg::core::RulesStore;

namespace icmg::cli {

static constexpr size_t INJECT_CAP_BYTES = 3 * 1024; // 3 KB

class RulesCommand : public BaseCommand {
public:
    std::string name()        const override { return "rules"; }
    std::string description() const override {
        return "Manage .icmgrules Memory Bank — sync, inject, list, enable/disable";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg rules <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  list [--json]     Show all rules (path, tag, active, bytes)\n"
            "  add <file>        Upsert a single file into the bank\n"
            "  disable <path>    Disable rule (excluded from inject)\n"
            "  enable  <path>    Enable rule\n"
            "  sync              Walk .icmgrules/ and upsert all *.md files\n"
            "  inject            Emit active rules as markdown for SessionStart\n\n"
            "Env:\n"
            "  ICMG_RULES_QUIET=1   inject returns empty (silent opt-out)\n\n"
            "Note: .icmgignore supports exact paths and simple *.ext globs.\n"
            "      Complex patterns (negations, **) are not supported — use .gitignore.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage(); return 0;
        }

        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        try {
            auto db = openProjectDb();
            if (!db) {
                std::cerr << "rules: no project DB (run `icmg init` first)\n";
                return 1;
            }
            RulesStore store(*db);

            if (sub == "list")    return doList(store, rest);
            if (sub == "add")     return doAdd(store, rest);
            if (sub == "disable") return doSetActive(store, rest, false);
            if (sub == "enable")  return doSetActive(store, rest, true);
            if (sub == "sync")    return doSync(store);
            if (sub == "inject")  return doInject(store);

            std::cerr << "rules: unknown subcommand '" << sub << "'\n";
            usage();
            return 1;
        } catch (const std::exception& e) {
            std::cerr << "rules: " << e.what() << "\n";
            return 1;
        }
    }

private:
    // Open project DB from cwd/.icmg/
    std::unique_ptr<core::Db> openProjectDb() const {
        // Walk up to find .icmg/
        fs::path cwd = fs::current_path();
        for (fs::path p = cwd; ; p = p.parent_path()) {
            fs::path candidate = p / ".icmg";
            if (fs::exists(candidate) && fs::is_directory(candidate)) {
                // Find *.db in .icmg/
                std::error_code ec;
                for (auto& e : fs::directory_iterator(candidate, ec)) {
                    if (e.path().extension() == ".db") {
                        auto db = std::make_unique<core::Db>(e.path().string());
                        core::Migrator migrator(
                            (p / "migrations").string());
                        migrator.runAll(*db);
                        return db;
                    }
                }
            }
            if (p == p.parent_path()) break;
        }
        return nullptr;
    }

    // Read frontmatter tag: field from markdown content
    static std::string extractTag(const std::string& content) {
        std::istringstream ss(content);
        std::string line;
        bool in_frontmatter = false;
        while (std::getline(ss, line)) {
            // Strip \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "---") {
                if (!in_frontmatter) { in_frontmatter = true; continue; }
                break; // end of frontmatter
            }
            if (in_frontmatter && line.rfind("tag:", 0) == 0) {
                std::string val = line.substr(4);
                // Trim
                size_t s = val.find_first_not_of(" \t");
                if (s != std::string::npos) val = val.substr(s);
                size_t e = val.find_last_not_of(" \t\r\n");
                if (e != std::string::npos) val = val.substr(0, e + 1);
                return val;
            }
        }
        return "";
    }

    int doList(RulesStore& store, const std::vector<std::string>& args) const {
        bool json_mode = hasFlag(args, "--json");
        auto rows = store.list();

        if (json_mode) {
            std::cout << "[\n";
            for (size_t i = 0; i < rows.size(); ++i) {
                auto& r = rows[i];
                std::cout << "  {\"path\":\"" << r.path << "\""
                          << ",\"tag\":\"" << r.tag << "\""
                          << ",\"active\":" << (r.active ? "true" : "false")
                          << ",\"bytes\":" << r.content.size()
                          << "}";
                if (i + 1 < rows.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "]\n";
        } else {
            if (rows.empty()) {
                std::cout << "(no rules — run `icmg rules sync` to load .icmgrules/)\n";
                return 0;
            }
            std::cout << "PATH                                TAG         ACTIVE  BYTES\n";
            std::cout << std::string(70, '-') << "\n";
            for (auto& r : rows) {
                printf("%-36s %-12s %-6s  %zu\n",
                    r.path.c_str(), r.tag.c_str(),
                    r.active ? "yes" : "no",
                    r.content.size());
            }
        }
        return 0;
    }

    int doAdd(RulesStore& store, const std::vector<std::string>& args) const {
        if (args.empty()) {
            std::cerr << "rules add: missing <file>\n"; return 1;
        }
        fs::path fpath(args[0]);
        std::ifstream f(fpath);
        if (!f) {
            std::cerr << "rules add: cannot open '" << args[0] << "'\n"; return 1;
        }
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        std::string tag = extractTag(content);
        std::string key = fpath.filename().string();
        store.upsert(key, content, tag);
        std::cout << "+ rules add: " << key << " (" << content.size() << " bytes)\n";
        return 0;
    }

    int doSetActive(RulesStore& store, const std::vector<std::string>& args, bool active) const {
        if (args.empty()) {
            std::cerr << "rules enable/disable: missing <path>\n"; return 1;
        }
        store.setActive(args[0], active);
        std::cout << (active ? "enabled" : "disabled") << ": " << args[0] << "\n";
        return 0;
    }

    int doSync(RulesStore& store) const {
        fs::path dir = fs::current_path() / ".icmgrules";
        if (!fs::exists(dir) || !fs::is_directory(dir)) {
            // Graceful no-op
            std::cout << "rules sync: .icmgrules/ not found — nothing to sync\n";
            return 0;
        }
        int synced = 0;
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) { ec.clear(); continue; }
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".md") continue;

            std::ifstream f(entry.path());
            if (!f) continue;
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            std::string tag = extractTag(content);
            // Key = relative path from cwd
            std::error_code rel_ec;
            std::string key = fs::relative(entry.path(), fs::current_path(), rel_ec).string();
            if (rel_ec) key = entry.path().filename().string();
            // Normalize path separators
            for (auto& c : key) if (c == '\\') c = '/';

            store.upsert(key, content, tag);
            ++synced;
        }
        std::cout << "rules sync: " << synced << " rule(s) loaded from .icmgrules/\n";
        return 0;
    }

    int doInject(RulesStore& store) const {
        // Opt-out env
        const char* quiet = std::getenv("ICMG_RULES_QUIET");
        if (quiet && std::string(quiet) == "1") return 0;

        auto rows = store.listActive();
        if (rows.empty()) return 0;

        std::string out = "## Project rules (.icmgrules)\n";
        size_t total = out.size();
        bool truncated = false;

        for (auto& r : rows) {
            if (truncated) break;
            std::string section = "### " + r.path + "\n" + r.content + "\n";
            if (total + section.size() > INJECT_CAP_BYTES) {
                // Truncate to remaining budget
                size_t remain = INJECT_CAP_BYTES - total;
                if (remain > 20) {
                    out += section.substr(0, remain - 14) + "\n...(truncated)\n";
                }
                truncated = true;
                break;
            }
            out += section;
            total += section.size();
        }

        std::cout << out;
        return 0;
    }
};

ICMG_REGISTER_COMMAND("rules", RulesCommand);

} // namespace icmg::cli
