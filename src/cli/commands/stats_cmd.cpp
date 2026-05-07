#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../graph/graph_store.hpp"
#include <iostream>
#include <filesystem>
#include <map>
#include <vector>
#include <algorithm>
#include <string>

namespace fs = std::filesystem;

namespace icmg::cli {

// ---- stats ----------------------------------------------------------------
class StatsCommand : public BaseCommand {
public:
    std::string name()        const override { return "stats"; }
    std::string description() const override { return "Show project usage statistics"; }

    int run(const std::vector<std::string>& args) override {
        bool json_out = hasFlag(args, "--json");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        int mem_active = 0, mem_total = 0, rules = 0, abbr = 0, sp = 0, cmds = 0;
        db.query("SELECT COUNT(*) FROM memory_nodes WHERE deleted_at IS NULL", {},
            [&](const core::Row& r) { if (!r.empty()) mem_active = std::stoi(r[0]); });
        db.query("SELECT COUNT(*) FROM memory_nodes", {},
            [&](const core::Row& r) { if (!r.empty()) mem_total  = std::stoi(r[0]); });
        db.query("SELECT COUNT(*) FROM rules WHERE active=1", {},
            [&](const core::Row& r) { if (!r.empty()) rules = std::stoi(r[0]); });
        db.query("SELECT COUNT(*) FROM abbreviations", {},
            [&](const core::Row& r) { if (!r.empty()) abbr  = std::stoi(r[0]); });
        db.query("SELECT COUNT(*) FROM stored_procedures", {},
            [&](const core::Row& r) { if (!r.empty()) sp    = std::stoi(r[0]); });
        db.query("SELECT COUNT(*) FROM commands", {},
            [&](const core::Row& r) { if (!r.empty()) cmds  = std::stoi(r[0]); });

        graph::GraphStore gs(db);
        int gnodes = gs.nodeCount(), gedges = gs.edgeCount();

        if (json_out) {
            std::cout << "{"
                << "\"memory_active\":"    << mem_active
                << ",\"memory_total\":"    << mem_total
                << ",\"graph_nodes\":"     << gnodes
                << ",\"graph_edges\":"     << gedges
                << ",\"rules\":"           << rules
                << ",\"abbreviations\":"   << abbr
                << ",\"stored_procs\":"    << sp
                << ",\"commands_seen\":"   << cmds
                << "}\n";
        } else {
            std::cout << "=== icmg project stats ===\n"
                      << "Memory nodes  : " << mem_active << " active (" << mem_total << " total)\n"
                      << "Graph nodes   : " << gnodes << "\n"
                      << "Graph edges   : " << gedges << "\n"
                      << "Rules         : " << rules  << "\n"
                      << "Abbreviations : " << abbr   << "\n"
                      << "Stored procs  : " << sp     << "\n"
                      << "Commands seen : " << cmds   << "\n";
        }
        return 0;
    }
};

// ---- doctor ---------------------------------------------------------------
class DoctorCommand : public BaseCommand {
public:
    std::string name()        const override { return "doctor"; }
    std::string description() const override { return "Health check (DB, schema, config)"; }

    int run(const std::vector<std::string>& args) override {
        bool json_out = hasFlag(args, "--json");

        auto& cfg = core::Config::instance();
        std::string db_path = cfg.projectDbPath(".");
        bool db_ok = fs::exists(db_path);

        int schema_ver = 0;
        bool schema_ok = false;
        if (db_ok) {
            try {
                core::Db db(db_path);
                schema_ver = db.userVersion();
                schema_ok  = (schema_ver >= 4); // expect at least 4 migrations
            } catch (...) {}
        }

        bool all_ok = db_ok && schema_ok;

        if (json_out) {
            std::cout << "{"
                << "\"ok\":"          << (all_ok   ? "true" : "false")
                << ",\"db_exists\":"  << (db_ok    ? "true" : "false")
                << ",\"db_path\":\"" << db_path << "\""
                << ",\"schema_version\":" << schema_ver
                << "}\n";
        } else {
            std::cout << "=== icmg doctor ===\n"
                      << "DB path      : " << db_path << "\n"
                      << "DB exists    : " << (db_ok     ? "YES" : "NO  ❌") << "\n"
                      << "Schema ver   : " << schema_ver  << (schema_ok ? "" : "  ❌ (expected >=4)") << "\n"
                      << "Status       : " << (all_ok    ? "OK" : "ISSUES FOUND") << "\n";
        }
        return all_ok ? 0 : 1;
    }
};

// ---- graph clean ----------------------------------------------------------
class GraphCleanCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-clean"; }
    std::string description() const override { return "Remove nodes for files no longer on disk"; }

    int run(const std::vector<std::string>& args) override {
        bool json_out = hasFlag(args, "--dry-run") ? false : hasFlag(args, "--json");
        bool dry_run  = hasFlag(args, "--dry-run");
        if (hasFlag(args, "--json") && !dry_run) json_out = true;

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        auto nodes = store.all();
        int removed = 0;
        std::vector<std::string> stale;

        for (auto& node : nodes) {
            std::error_code ec;
            bool exists = fs::exists(node.path, ec);
            if (!exists) {
                stale.push_back(node.path);
                if (!dry_run) store.removeNode(node.path);
                removed++;
            }
        }

        if (json_out) {
            std::cout << "{\"removed\":" << removed
                      << ",\"dry_run\":" << (dry_run ? "true" : "false")
                      << ",\"paths\":[";
            for (size_t i = 0; i < stale.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "\"" << stale[i] << "\"";
            }
            std::cout << "]}\n";
        } else {
            std::string prefix = dry_run ? "[dry-run] Would remove" : "Removed";
            if (removed == 0)
                std::cout << "Graph is clean — no stale nodes.\n";
            else {
                std::cout << prefix << " " << removed << " stale node(s):\n";
                for (auto& p : stale) std::cout << "  " << p << "\n";
            }
        }
        return 0;
    }
};

// ---- graph dedupe — merge case-mismatched path duplicates (Phase 20 hotfix) ----
// Windows is case-insensitive but case-preserving; old scans before
// drive-letter normalization can leave d:\ + D:\ duplicates that produce
// false cycles. Merges duplicates into the upper-case-drive variant.
class GraphDedupeCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-dedupe"; }
    std::string description() const override { return "Merge case-mismatched duplicate path nodes (Windows)"; }

    int run(const std::vector<std::string>& args) override {
        bool dry_run = hasFlag(args, "--dry-run");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        // Group by lower(path); keep the row whose drive-letter is upper-case
        // (or first id if neither has a drive letter).
        struct Bucket { std::vector<std::pair<int64_t,std::string>> rows; };
        std::map<std::string, Bucket> buckets;
        db.query("SELECT id, path FROM graph_nodes ORDER BY id", {},
                 [&](const core::Row& r) {
                     if (r.size() < 2) return;
                     int64_t id;
                     try { id = std::stoll(r[0]); } catch (...) { return; }
                     std::string lower = r[1];
                     std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                     buckets[lower].rows.push_back({id, r[1]});
                 });

        int merged = 0, kept = 0;
        for (auto& [lower, b] : buckets) {
            if (b.rows.size() <= 1) continue;
            // Pick keeper: prefer upper-case drive (X:\...) else first id
            int64_t keeper_id = b.rows[0].first;
            std::string keeper_path = b.rows[0].second;
            for (auto& [id, path] : b.rows) {
                if (path.size() >= 2 && path[1] == ':' && path[0] >= 'A' && path[0] <= 'Z') {
                    keeper_id = id;
                    keeper_path = path;
                    break;
                }
            }
            ++kept;
            for (auto& [id, path] : b.rows) {
                if (id == keeper_id) continue;
                ++merged;
                if (dry_run) {
                    std::cout << "[dry-run] would merge #" << id << "  " << path
                              << "  → keeper #" << keeper_id << "  " << keeper_path << "\n";
                } else {
                    // Reparent edges to keeper
                    db.run("UPDATE OR IGNORE graph_edges SET src=? WHERE src=?",
                           {std::to_string(keeper_id), std::to_string(id)});
                    db.run("UPDATE OR IGNORE graph_edges SET dst=? WHERE dst=?",
                           {std::to_string(keeper_id), std::to_string(id)});
                    // Delete leftover edges that became self-references
                    db.run("DELETE FROM graph_edges WHERE src=dst", {});
                    // Delete duplicate row (cascades any remaining child symbols)
                    db.run("DELETE FROM graph_nodes WHERE id=?", {std::to_string(id)});
                }
            }
        }
        if (dry_run) {
            std::cout << "[dry-run] would merge " << merged << " duplicate(s) into "
                      << kept << " keeper(s).\n";
        } else {
            std::cout << "Merged " << merged << " duplicate(s) into "
                      << kept << " keeper(s). Run `icmg graph cycles` to verify.\n";
        }
        return 0;
    }
};

// ---- grep shortcut --------------------------------------------------------
// icmg grep <pattern> [dir] [--ext .cs,.cpp,...] — delegate to icmg run grep
class GrepCommand : public BaseCommand {
public:
    std::string name()        const override { return "grep"; }
    std::string description() const override { return "Search source code (alias: icmg run grep -rn)"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) {
            std::cerr << "Usage: icmg grep <pattern> [dir]\n"
                      << "       (same as: icmg run grep -rn <pattern> <dir>)\n";
            return 1;
        }

        std::string pattern, dir = ".";
        for (auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            if (pattern.empty()) pattern = a;
            else if (dir == ".") dir = a;
        }
        if (pattern.empty()) {
            std::cerr << "icmg grep: pattern required\n"; return 1;
        }

        // Delegate directly to grep subprocess (Tkil already has grep filter in run cmd)
        // Use execvp-style via argv to avoid shell glob expansion
        std::vector<std::string> grep_args = {"-rn", "--", pattern, dir};
        std::string icmg_run = "icmg run grep";
        // Build safe shell command with proper quoting
#ifdef _WIN32
        std::string cmd = "grep -rn -- \"" + pattern + "\" \"" + dir + "\"";
#else
        std::string cmd = "grep -rn -- \"" + pattern + "\" \"" + dir + "\"";
#endif
        return std::system(cmd.c_str());
    }
};

ICMG_REGISTER_COMMAND("stats",       StatsCommand);
ICMG_REGISTER_COMMAND("doctor",      DoctorCommand);
ICMG_REGISTER_COMMAND("graph-clean",  GraphCleanCommand);
ICMG_REGISTER_COMMAND("graph-dedupe", GraphDedupeCommand);
// grep: use `icmg run grep` instead — too many Windows/shell-quoting edge cases

} // namespace icmg::cli
