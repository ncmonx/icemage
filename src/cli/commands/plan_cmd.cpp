// icmg plan — plan/phase markdown files ↔ context_nodes graph bridge.
//
// Subcommands:
//   import [--file PATH] [--dir PATH] [--dry-run] [--slim] [--no-backup]
//       Parse plan/phase markdown sections → upsert context_nodes.
//       Auto-detects PROGRESS.md, PLAN.md, PHASES.md, docs/plans/*.md,
//       .icmg/plans/*.md when --file/--dir omitted.
//       --slim      After import, replace file with pointer stub.
//                   Backup saved to .icmg/ before overwrite.
//       --no-backup Skip backup when used with --slim.
//   restore [--file PATH]
//       Restore plan file from latest backup (created by import --slim).
//   list [--tier hot|cold] [--inactive] [--json]
//       Print stored plan context_nodes (node_key starts with "plan-").
//   diff [--file PATH]
//       Compare live plan file sections vs stored nodes (stale/new/changed).

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

static bool isHotPlanSection(const std::string& title) {
    std::string t = title;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    static const char* HOT[] = {
        "current", "active", "progress", "status", "next",
        "in progress", "session log", "phase plan", "overview"
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

// Collect auto-detected plan file paths
static std::vector<std::string> autoDetectPlanFiles() {
    std::vector<std::string> files;

    // Fixed well-known filenames at project root
    static const char* KNOWN[] = {
        "PROGRESS.md", "PLAN.md", "PHASES.md"
    };
    for (auto name : KNOWN) {
        if (fs::exists(name)) files.push_back(core::absolutePath(name));
    }

    // docs/plans/*.md
    fs::path docs_plans = fs::path("docs") / "plans";
    if (fs::exists(docs_plans) && fs::is_directory(docs_plans)) {
        for (auto& e : fs::directory_iterator(docs_plans)) {
            if (e.is_regular_file() && e.path().extension() == ".md")
                files.push_back(e.path().string());
        }
    }

    // .icmg/plans/*.md
    fs::path icmg_plans = fs::path(".icmg") / "plans";
    if (fs::exists(icmg_plans) && fs::is_directory(icmg_plans)) {
        for (auto& e : fs::directory_iterator(icmg_plans)) {
            if (e.is_regular_file() && e.path().extension() == ".md")
                files.push_back(e.path().string());
        }
    }

    return files;
}

// ---- do* methods ------------------------------------------------------------

// Forward declarations
static int doSlim(ContextNodeStore& store, const std::vector<std::string>& args);
static int doRestore(const std::vector<std::string>& args);

static int doImport(ContextNodeStore& store, const std::vector<std::string>& args,
                    const std::string& db_path) {
    std::string file_arg;
    std::string dir_arg;
    bool dry_run   = false;
    bool do_slim   = false;
    bool no_backup = false;

    for (size_t i = 0; i < args.size(); ++i) {
        if      (args[i] == "--file" && i + 1 < args.size()) file_arg = args[++i];
        else if (args[i] == "--dir"  && i + 1 < args.size()) dir_arg  = args[++i];
        else if (args[i] == "--dry-run")   dry_run   = true;
        else if (args[i] == "--slim")      do_slim   = true;
        else if (args[i] == "--no-backup") no_backup = true;
    }

    std::vector<std::string> files;

    if (!file_arg.empty()) {
        files.push_back(file_arg);
    } else if (!dir_arg.empty()) {
        fs::path dir(dir_arg);
        if (!fs::exists(dir) || !fs::is_directory(dir)) {
            std::cerr << "plan import: directory not found: " << dir_arg << "\n";
            return 1;
        }
        for (auto& e : fs::directory_iterator(dir)) {
            if (e.is_regular_file() && e.path().extension() == ".md")
                files.push_back(e.path().string());
        }
    } else {
        files = autoDetectPlanFiles();
    }

    if (files.empty()) {
        std::cerr << "plan import: no plan files found. Use --file PATH or --dir PATH.\n";
        return 1;
    }

    int total = 0;
    for (auto& fpath : files) {
        std::string text = readFile(fpath);
        if (text.empty()) {
            std::cerr << "plan import: cannot read " << fpath << "\n";
            continue;
        }

        // Skip files already slimmed by a previous import --slim run
        if (text.find("<!-- icmg-slim") != std::string::npos) {
            std::cout << "skipping already-slim: " << fpath << "\n";
            continue;
        }

        auto sections = parseSections(text);
        for (auto& sec : sections) {
            std::string key_suffix = slugify(sec.title);
            if (key_suffix.empty()) continue;

            ContextNode node;
            node.node_key    = "plan-" + key_suffix;
            node.title       = sec.title;
            node.content     = sec.content;
            node.source_file = core::absolutePath(fpath);
            node.tier        = isHotPlanSection(sec.title) ? "hot" : "cold";
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
        if      (args[i] == "--tier" && i + 1 < args.size()) tier = args[++i];
        else if (args[i] == "--inactive") show_inactive = true;
        else if (args[i] == "--json")     json_out = true;
    }

    auto all_nodes = store.list(tier, !show_inactive);

    // Filter to plan nodes only (node_key starts with "plan-")
    std::vector<ContextNode> nodes;
    for (auto& n : all_nodes) {
        if (n.node_key.size() >= 5 && n.node_key.substr(0, 5) == "plan-")
            nodes.push_back(n);
    }

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
              << std::setw(38) << "NODE_KEY"
              << std::setw(6)  << "ACT"
              << "TITLE\n";
    std::cout << std::string(74, '-') << "\n";

    for (auto& n : nodes) {
        std::cout << std::left
                  << std::setw(4)  << n.id
                  << std::setw(8)  << n.tier
                  << std::setw(38) << n.node_key.substr(0, 37)
                  << std::setw(6)  << (n.active ? "yes" : "no")
                  << n.title << "\n";
    }
    std::cout << "\n" << nodes.size() << " plan node(s)\n";
    return 0;
}

static int doDiff(ContextNodeStore& store, const std::vector<std::string>& args) {
    std::string file_arg;
    for (size_t i = 0; i < args.size(); ++i)
        if (args[i] == "--file" && i + 1 < args.size()) file_arg = args[++i];

    std::string fpath = file_arg.empty() ? "PROGRESS.md" : file_arg;
    if (!fs::exists(fpath)) {
        std::cerr << "plan diff: file not found: " << fpath << "\n";
        return 1;
    }

    std::string text = readFile(fpath);
    auto live = parseSections(text);
    std::string fabs = core::absolutePath(fpath);

    bool any = false;
    for (auto& sec : live) {
        std::string key = "plan-" + slugify(sec.title);
        if (slugify(sec.title).empty()) continue;
        auto stored = store.get(key);
        if (!stored) {
            std::cout << "[NEW]     " << key << " — " << sec.title << "\n";
            any = true;
        } else if (contentHash(stored->content) != contentHash(sec.content)) {
            std::cout << "[CHANGED] " << key << " — " << sec.title << "\n";
            any = true;
        }
    }

    // Check for removed sections (plan nodes from this source file)
    auto all_stored = store.list("", true);
    for (auto& n : all_stored) {
        if (n.node_key.size() < 5 || n.node_key.substr(0, 5) != "plan-") continue;
        if (n.source_file != fabs) continue;
        bool found = false;
        for (auto& sec : live)
            if ("plan-" + slugify(sec.title) == n.node_key) { found = true; break; }
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
        if      (args[i] == "--file" && i + 1 < args.size()) file_arg = args[++i];
        else if (args[i] == "--out"  && i + 1 < args.size()) out_arg  = args[++i];
    }

    std::string fpath = file_arg.empty() ? "PROGRESS.md" : file_arg;
    std::string text = readFile(fpath);
    auto sections = parseSections(text);

    std::string stem = fs::path(fpath).stem().string();

    if (sections.empty() && !fs::exists(fpath)) {
        // No file — generate from stored plan nodes
        auto all_nodes = store.list();
        std::ostringstream out;
        out << "# Plan (managed by icmg)\n";
        out << "<!-- icmg-slim: generated by `icmg plan import --slim`. Restore: `icmg plan restore --file "
            << fpath << "` -->\n\n";
        out << "> Hooks inject relevant sections per-session (hot) and per-prompt (cold, BM25).\n";
        out << "> Browse: `icmg plan list` | `icmg knowledge --html` | restore: `icmg plan restore`\n\n";
        out << "## Sections\n\n";
        for (auto& n : all_nodes) {
            if (n.node_key.size() < 5 || n.node_key.substr(0, 5) != "plan-") continue;
            out << "- `" << n.node_key << "` [" << n.tier << "] " << n.title << "\n";
        }
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
    out << "# Plan (managed by icmg)\n";
    out << "<!-- icmg-slim: generated by `icmg plan import --slim`. Restore: `icmg plan restore --file "
        << fpath << "` -->\n\n";
    out << "> Hooks inject relevant sections per-session (hot) and per-prompt (cold, BM25).\n";
    out << "> Browse: `icmg plan list` | `icmg knowledge --html` | restore: `icmg plan restore`\n\n";
    for (auto& sec : sections) {
        std::string key = "plan-" + slugify(sec.title);
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

    std::string fpath = file_arg.empty() ? "PROGRESS.md" : file_arg;
    fs::path file_parent = fs::path(fpath).parent_path();
    std::string stem = fs::path(fpath).stem().string();

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
            // Match backup files for this stem (e.g. "PROGRESS-backup-*.md")
            if (fn.find(stem) != std::string::npos &&
                fn.find("backup") != std::string::npos &&
                e.path().extension() == ".md")
                backups.push_back(e.path());
        }
    }

    if (backups.empty()) {
        std::cerr << "plan restore: no backup found for '" << stem << "' (searched";
        for (auto& d : search_dirs) std::cerr << " " << d.string();
        std::cerr << ")\n";
        return 1;
    }
    std::sort(backups.begin(), backups.end());
    auto latest = backups.back();

    std::error_code ec;
    fs::copy_file(latest, fpath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "plan restore: " << ec.message() << "\n";
        return 1;
    }
    std::cout << "restored " << fpath << " from " << latest.string() << "\n";
    return 0;
}

// ---- command ----------------------------------------------------------------

class PlanCommand : public BaseCommand {
public:
    std::string name()        const override { return "plan"; }
    std::string description() const override { return "Plan/phase markdown files <-> context_nodes graph bridge"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg plan <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  import [--file PATH] [--dir PATH] [--dry-run] [--slim] [--no-backup]\n"
            "      Parse plan/phase markdown sections into context_nodes DB.\n"
            "      Auto-detects PROGRESS.md, PLAN.md, PHASES.md, docs/plans/*.md,\n"
            "      .icmg/plans/*.md when --file/--dir omitted.\n"
            "      --slim      After import, replace file with pointer stub.\n"
            "                  Backup saved to .icmg/ before overwrite.\n"
            "      --no-backup Skip backup when used with --slim.\n"
            "  restore [--file PATH]\n"
            "      Restore plan file from latest backup (default: PROGRESS.md).\n"
            "  list [--tier hot|cold] [--inactive] [--json]\n"
            "      List stored plan context_nodes (node_key prefix: plan-).\n"
            "  diff [--file PATH]\n"
            "      Show NEW/CHANGED/REMOVED sections vs stored nodes.\n"
            "      Default file: PROGRESS.md\n"
            "  slim [--file PATH] [--out PATH]\n"
            "      Generate pointer-only stub (stdout or --out file).\n"
            "      Default file: PROGRESS.md\n\n"
            "Hot-tier heuristic: sections whose title contains 'current', 'active',\n"
            "  'progress', 'status', 'next', 'in progress', 'session log',\n"
            "  'phase plan', or 'overview' are stored as tier=hot.\n";
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

        if (sub == "import")  return doImport(store, rest, cfg.projectDbPath("."));
        if (sub == "restore") return doRestore(rest);
        if (sub == "list")    return doList(store, rest);
        if (sub == "diff")    return doDiff(store, rest);
        if (sub == "slim")    return doSlim(store, rest);

        std::cerr << "plan: unknown subcommand '" << sub << "'. Try --help.\n";
        return 1;
    }
};

ICMG_REGISTER_COMMAND("plan", PlanCommand);

} // namespace icmg::cli
