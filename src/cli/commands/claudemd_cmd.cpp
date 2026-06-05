// icmg claudemd — CLAUDE.md ↔ context_nodes graph bridge.
//
// Subcommands:
//   import [--file PATH] [--all] [--dry-run]
//       Parse CLAUDE.md sections → upsert context_nodes.
//       Auto-detects ~/.claude/CLAUDE.md + ./CLAUDE.md when --file omitted.
//       --all: scan all registered projects.
//   list [--tier hot|cold|skill] [--inactive]
//       Print stored context_nodes.
//   diff [--file PATH]
//       Compare live CLAUDE.md sections vs stored nodes (stale/new/changed).
//   slim [--file PATH] [--out PATH]
//       Print/write slim pointer-only CLAUDE.md stub.
//   export — alias for import.

#include "../base_command.hpp"
#include "../../core/path_utils.hpp"   // absolutePath: no-throw (err126-safe)
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/context_node_store.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace fs = std::filesystem;
using namespace icmg::core;

namespace icmg::cli {

// ---- helpers ----------------------------------------------------------------

static std::string slugify(const std::string& title) {
    std::string slug;
    for (unsigned char c : title) {
        if (std::isalnum(c)) {
            slug += static_cast<char>(std::tolower(c));
        } else if (c == ' ' || c == '_' || c == '-') {
            if (!slug.empty() && slug.back() != '-') slug += '-';
        }
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug;
}

static bool isHotSection(const std::string& title) {
    std::string t = title;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    static const char* HOT[] = {
        "project overview", "overview", "architecture", "coding conventions",
        "key design", "design decisions", "build instructions", "build"
    };
    for (auto h : HOT) {
        if (t.find(h) != std::string::npos) return true;
    }
    return false;
}

struct Section {
    std::string title;
    std::string content;
};

static std::vector<Section> parseSections(const std::string& text) {
    std::vector<Section> sections;
    std::istringstream iss(text);
    std::string line;
    Section cur;
    bool in_section = false;

    while (std::getline(iss, line)) {
        if (line.size() >= 3 && line.substr(0, 3) == "## ") {
            if (in_section && !cur.title.empty()) {
                // Trim trailing whitespace from content
                while (!cur.content.empty() &&
                       (cur.content.back() == '\n' || cur.content.back() == '\r'))
                    cur.content.pop_back();
                sections.push_back(cur);
            }
            cur.title   = line.substr(3);
            cur.content = "";
            in_section  = true;
        } else if (in_section) {
            cur.content += line + "\n";
        }
    }
    if (in_section && !cur.title.empty()) {
        while (!cur.content.empty() &&
               (cur.content.back() == '\n' || cur.content.back() == '\r'))
            cur.content.pop_back();
        sections.push_back(cur);
    }
    return sections;
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string contentHash(const std::string& s) {
    // FNV-1a 32-bit — stored as hex string for diff comparison
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08x", h);
    return std::string(buf);
}

// ---- do* methods ------------------------------------------------------------

// Forward declarations (doImport --slim calls doSlim)
static int doSlim(ContextNodeStore& store, const std::vector<std::string>& args);
static int doRestore(const std::vector<std::string>& args);

static int doImport(ContextNodeStore& store, const std::vector<std::string>& args,
                    const std::string& db_path) {
    std::string file_arg;
    bool dry_run   = false;
    bool do_slim   = false;
    bool no_backup = false;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--file" && i + 1 < args.size()) file_arg = args[++i];
        else if (args[i] == "--dry-run")   dry_run   = true;
        else if (args[i] == "--slim")      do_slim   = true;
        else if (args[i] == "--no-backup") no_backup = true;
    }

    std::vector<std::string> files;
    if (!file_arg.empty()) {
        files.push_back(file_arg);
    } else {
        // Auto-detect global + project CLAUDE.md
        std::string home = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") :
                          (std::getenv("HOME") ? std::getenv("HOME") : "");
        if (!home.empty()) {
            std::string g = home + "/.claude/CLAUDE.md";
            if (fs::exists(g)) files.push_back(g);
        }
        if (fs::exists("CLAUDE.md")) files.push_back(core::absolutePath("CLAUDE.md"));
        if (fs::exists(".claude/CLAUDE.md"))
            files.push_back(core::absolutePath(".claude/CLAUDE.md"));
    }

    if (files.empty()) {
        std::cerr << "claudemd import: no CLAUDE.md found. Use --file PATH.\n";
        return 1;
    }

    int total = 0;
    for (auto& fpath : files) {
        std::string text = readFile(fpath);
        if (text.empty()) {
            std::cerr << "claudemd import: cannot read " << fpath << "\n";
            continue;
        }

        // Skip files already slimmed by a previous import --slim run
        if (text.find("<!-- icmg-slim") != std::string::npos) {
            std::cout << "skipping already-slim: " << fpath << "\n";
            continue;
        }

        auto sections = parseSections(text);
        for (auto& sec : sections) {
            ContextNode node;
            node.node_key    = slugify(sec.title);
            if (node.node_key.empty()) continue;
            node.title       = sec.title;
            node.content     = sec.content;
            node.source_file = fpath;
            node.tier        = isHotSection(sec.title) ? "hot" :
                               (sec.content.size() > 3000 ? "frozen" : "cold");
            node.tags        = "[]";
            node.active      = true;

            if (dry_run) {
                std::cout << "[dry-run] " << node.tier << " | " << node.node_key
                          << " | " << node.title << "\n";
            } else {
                store.upsert(node);
            }
            ++total;
        }
        std::cout << (dry_run ? "[dry-run] would import " : "imported ")
                  << sections.size() << " sections from " << fpath << "\n";

        // --slim: backup original then overwrite with pointer stub
        if (do_slim && !dry_run && !sections.empty()) {
            // Determine backup directory (.icmg/ next to file, or ~/.icmg/)
            fs::path file_parent = fs::path(fpath).parent_path();
            fs::path backup_dir;
            fs::path local_icmg = file_parent / ".icmg";
            if (fs::exists(local_icmg) && fs::is_directory(local_icmg)) {
                backup_dir = local_icmg;
            } else {
                std::string home = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") :
                                  (std::getenv("HOME") ? std::getenv("HOME") : "");
                backup_dir = fs::path(home) / ".icmg";
                std::error_code mec;
                fs::create_directories(backup_dir, mec);
            }

            if (!no_backup) {
                auto now = std::chrono::system_clock::now();
                std::time_t t = std::chrono::system_clock::to_time_t(now);
                char ts[32];
                std::strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", std::localtime(&t));
                std::string stem = fs::path(fpath).stem().string();
                fs::path backup_path = backup_dir / (stem + "-backup-" + ts + ".md");

                std::error_code ec;
                fs::copy_file(fpath, backup_path, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    std::cerr << "  [warn] backup failed: " << ec.message() << "\n";
                } else {
                    std::cout << "  backup: " << backup_path.string() << "\n";
                }
            }

            int sr = doSlim(store, {"--file", fpath, "--out", fpath});
            if (sr == 0) {
                std::cout << "  slim:   " << fpath << " → pointer stub\n";
            } else {
                std::cerr << "  [warn] slim failed for " << fpath << "\n";
            }
        }
    }
    std::cout << "total: " << total << " nodes " << (dry_run ? "(dry-run)" : "upserted")
              << " from " << files.size() << " file(s)\n";
    return 0;
}

static int doList(ContextNodeStore& store, const std::vector<std::string>& args) {
    std::string tier;
    bool show_inactive = false;
    bool json_out = false;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--tier" && i + 1 < args.size()) tier = args[++i];
        else if (args[i] == "--inactive") show_inactive = true;
        else if (args[i] == "--json")     json_out = true;
    }

    auto nodes = store.list(tier, !show_inactive);

    if (json_out) {
        std::cout << "[\n";
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto& n = nodes[i];
            std::cout << "  {\"node_key\":\"" << n.node_key
                      << "\",\"title\":\"" << n.title
                      << "\",\"tier\":\"" << n.tier
                      << "\",\"active\":" << (n.active ? "true" : "false")
                      << ",\"source\":\"" << n.source_file << "\"}"
                      << (i + 1 < nodes.size() ? "," : "") << "\n";
        }
        std::cout << "]\n";
        return 0;
    }

    std::cout << std::left
              << std::setw(4)  << "ID"
              << std::setw(8)  << "TIER"
              << std::setw(35) << "NODE_KEY"
              << std::setw(6)  << "ACT"
              << "TITLE\n";
    std::cout << std::string(70, '-') << "\n";

    for (auto& n : nodes) {
        std::cout << std::left
                  << std::setw(4)  << n.id
                  << std::setw(8)  << n.tier
                  << std::setw(35) << n.node_key.substr(0, 34)
                  << std::setw(6)  << (n.active ? "yes" : "no")
                  << n.title << "\n";
    }
    std::cout << "\n" << nodes.size() << " node(s)\n";
    return 0;
}

static int doDiff(ContextNodeStore& store, const std::vector<std::string>& args) {
    std::string file_arg;
    for (size_t i = 0; i < args.size(); ++i)
        if (args[i] == "--file" && i + 1 < args.size()) file_arg = args[++i];

    std::string fpath = file_arg.empty() ? "CLAUDE.md" : file_arg;
    if (!fs::exists(fpath)) {
        std::cerr << "claudemd diff: file not found: " << fpath << "\n";
        return 1;
    }

    std::string text = readFile(fpath);
    auto live = parseSections(text);

    bool any = false;
    for (auto& sec : live) {
        std::string key = slugify(sec.title);
        if (key.empty()) continue;
        auto stored = store.get(key);
        if (!stored) {
            std::cout << "[NEW]     " << key << " — " << sec.title << "\n";
            any = true;
        } else if (contentHash(stored->content) != contentHash(sec.content)) {
            std::cout << "[CHANGED] " << key << " — " << sec.title << "\n";
            any = true;
        }
    }

    // Check for removed sections
    auto all_stored = store.list("", true);
    std::string fabs = core::absolutePath(fpath);
    for (auto& n : all_stored) {
        if (n.source_file != fabs) continue;
        bool found = false;
        for (auto& sec : live)
            if (slugify(sec.title) == n.node_key) { found = true; break; }
        if (!found) {
            std::cout << "[REMOVED] " << n.node_key << " — " << n.title << "\n";
            any = true;
        }
    }

    if (!any) std::cout << "no drift detected\n";
    return 0;
}

static int doSlim(ContextNodeStore& store, const std::vector<std::string>& args) {
    std::string file_arg, out_arg;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--file" && i + 1 < args.size()) file_arg = args[++i];
        else if (args[i] == "--out"  && i + 1 < args.size()) out_arg  = args[++i];
    }

    std::string fpath = file_arg.empty() ? "CLAUDE.md" : file_arg;
    std::string text = readFile(fpath);
    auto sections = parseSections(text);
    if (sections.empty() && !fs::exists(fpath)) {
        // No CLAUDE.md — generate from stored nodes
        auto nodes = store.list();
        std::ostringstream out;
        out << "# Context Graph (managed by icmg)\n";
        out << "<!-- icmg-slim: generated by `icmg claudemd import --slim`. Restore: `icmg claudemd restore` -->\n\n";
        out << "> Hooks inject relevant sections per-session (hot) and per-prompt (cold, BM25).\n";
        out << "> Browse: `icmg knowledge list` | `icmg knowledge --html` | restore: `icmg claudemd restore`\n\n";
        out << "## Sections\n\n";
        for (auto& n : nodes)
            out << "- `" << n.node_key << "` [" << n.tier << "] " << n.title << "\n";
        auto slim = out.str();
        if (out_arg.empty()) { std::cout << slim; }
        else {
            std::ofstream f(out_arg);
            f << slim;
            std::cout << "slim written to " << out_arg << "\n";
        }
        return 0;
    }

    std::ostringstream out;
    out << "# Context Graph (managed by icmg)\n";
    out << "<!-- icmg-slim: generated by `icmg claudemd import --slim`. Restore: `icmg claudemd restore` -->\n\n";
    out << "> Hooks inject relevant sections per-session (hot) and per-prompt (cold, BM25).\n";
    out << "> Browse: `icmg knowledge list` | `icmg knowledge --html` | restore: `icmg claudemd restore`\n\n";
    for (auto& sec : sections) {
        std::string key = slugify(sec.title);
        out << "## " << sec.title << "\n";
        out << "> node: `" << key << "` — `icmg knowledge get " << key << "`\n\n";
    }

    std::string slim = out.str();
    if (out_arg.empty()) {
        std::cout << slim;
    } else {
        std::ofstream f(out_arg);
        f << slim;
        std::cout << "slim written to " << out_arg << "\n";
    }
    return 0;
}

static int doRestore(const std::vector<std::string>& args) {
    std::string file_arg;
    for (size_t i = 0; i < args.size(); ++i)
        if (args[i] == "--file" && i + 1 < args.size()) file_arg = args[++i];

    std::string fpath = file_arg.empty() ? "CLAUDE.md" : file_arg;
    fs::path file_parent = fs::path(fpath).parent_path();

    // Search .icmg/ next to file, then ~/.icmg/
    std::vector<fs::path> search_dirs;
    fs::path local_icmg = file_parent / ".icmg";
    if (fs::exists(local_icmg)) search_dirs.push_back(local_icmg);
    std::string home = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") :
                      (std::getenv("HOME") ? std::getenv("HOME") : "");
    if (!home.empty()) search_dirs.push_back(fs::path(home) / ".icmg");

    std::vector<fs::path> backups;
    for (auto& dir : search_dirs) {
        if (!fs::exists(dir)) continue;
        for (auto& e : fs::directory_iterator(dir)) {
            std::string fn = e.path().filename().string();
            if (fn.find("CLAUDE") != std::string::npos &&
                fn.find("backup") != std::string::npos &&
                e.path().extension() == ".md")
                backups.push_back(e.path());
        }
    }

    if (backups.empty()) {
        std::cerr << "claudemd restore: no backup found (searched";
        for (auto& d : search_dirs) std::cerr << " " << d.string();
        std::cerr << ")\n";
        return 1;
    }
    std::sort(backups.begin(), backups.end());
    auto latest = backups.back();

    std::error_code ec;
    fs::copy_file(latest, fpath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "claudemd restore: " << ec.message() << "\n";
        return 1;
    }
    std::cout << "restored " << fpath << " from " << latest.string() << "\n";
    return 0;
}

// ---- command ----------------------------------------------------------------

class ClaudemdCommand : public BaseCommand {
public:
    std::string name()        const override { return "claudemd"; }
    std::string description() const override { return "CLAUDE.md <-> context_nodes graph bridge"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg claudemd <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  import [--file PATH] [--all] [--dry-run] [--slim] [--no-backup]\n"
            "      Parse CLAUDE.md sections into context_nodes DB.\n"
            "      Auto-detects ~/.claude/CLAUDE.md + ./CLAUDE.md when omitted.\n"
            "      --slim      After import, replace CLAUDE.md with pointer stub.\n"
            "                  Backup saved to .icmg/ before overwrite.\n"
            "      --no-backup Skip backup when used with --slim.\n"
            "  restore [--file PATH]\n"
            "      Restore CLAUDE.md from latest backup (created by import --slim).\n"
            "  export      Alias for import.\n"
            "  list [--tier hot|cold|skill] [--inactive] [--json]\n"
            "      List stored context nodes.\n"
            "  diff [--file PATH]\n"
            "      Show NEW/CHANGED/REMOVED sections vs stored nodes.\n"
            "  slim [--file PATH] [--out PATH]\n"
            "      Generate pointer-only CLAUDE.md stub (stdout or --out file).\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        ContextNodeStore store(db);

        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        if (sub == "import" || sub == "export") return doImport(store, rest, cfg.projectDbPath("."));
        if (sub == "restore")                    return doRestore(rest);
        if (sub == "list")                       return doList(store, rest);
        if (sub == "diff")                       return doDiff(store, rest);
        if (sub == "slim")                       return doSlim(store, rest);

        std::cerr << "claudemd: unknown subcommand '" << sub << "'. Try --help.\n";
        return 1;
    }
};

ICMG_REGISTER_COMMAND("claudemd", ClaudemdCommand);

} // namespace icmg::cli
