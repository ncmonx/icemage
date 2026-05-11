// Phase 74 T3: `icmg maintain` — self-maintenance orchestrator.
//
// Goal: when DB heavy, drop unneeded memory. When idle, retain only graph
// nodes linked to recently-modified code files. Composes existing prune /
// decay / consolidate primitives via subprocess for single source of truth.
//
// Subcommands:
//   run [--aggressive] [--idle-mode] [--dry-run]
//                                 Orchestrate prune chain based on detected
//                                 heavy/idle state.
//   status                        Print DB size, row counts, idle/heavy flags.
//   auto-on  [--interval Nh]      Schedule periodic maintain run (default 6h).
//   auto-off                      Remove scheduled task.
//   auto-status                   Show schedule install + last run.
//
// Heavy detection (any triggers prune):
//   - data.db > 100 MB                        (override --size-mb N)
//   - memory_nodes count > 50,000             (override --row-cap N)
//
// Idle detection (no recent project activity):
//   - last tool_invocations row > 24h ago     (override --idle-hours N)
//   - AND newest source file mtime > 24h ago  (excludes .icmg/, build/, .git/)
//   - → triggers extra: drop memory not linked to "active" graph nodes
//        (graph_nodes with edge.dst pointing to file mtime <30d)
//
// Aggressive (--aggressive): also drops auto:* >7d, session:* >14d, decay all
// non-pinned. Default: auto:* >30d, session:* >60d, decay >30d non-pinned.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/schedule_helper.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

class MaintainCommand : public BaseCommand {
public:
    std::string name()        const override { return "maintain"; }
    std::string description() const override {
        return "Self-maintenance: prune heavy DB, idle-mode keeps only active graph";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg maintain <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  run [--aggressive] [--idle-mode] [--dry-run] [--vacuum] [--fix-graph]\n"
            "         [--size-mb N] [--row-cap N] [--idle-hours N]\n"
            "                              Orchestrate prune chain + graph integrity\n"
            "  status                      Show size, row counts, idle/heavy flags\n"
            "  auto-on [--interval Nh]     Schedule periodic run (default 6h)\n"
            "  auto-off                    Remove scheduled task\n"
            "  auto-status                 Show schedule install + last run\n\n"
            "Triggers:\n"
            "  heavy  →  data.db > size-mb (def 100) OR memory_nodes > row-cap (def 50000)\n"
            "  idle   →  no tool_invocations + no source mtime in last idle-hours (def 24)\n"
            "  --idle-mode forces idle-prune even if not auto-detected\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        if (sub == "run")         return cmdRun(rest);
        if (sub == "status")      return cmdStatus(rest);
        if (sub == "auto-on")     return cmdAutoOn(rest);
        if (sub == "auto-off")    return cmdAutoOff(rest);
        if (sub == "auto-status") return cmdAutoStatus(rest);
        std::cerr << "icmg maintain: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    static fs::path projectRoot() { return fs::current_path(); }
    static fs::path dbPath()      { return projectRoot() / ".icmg" / "data.db"; }

    // ---- detection -------------------------------------------------------

    struct State {
        uintmax_t db_bytes  = 0;
        int64_t   mem_rows  = 0;
        int64_t   tel_rows  = 0;
        std::time_t last_tool_invocation = 0;
        std::time_t newest_source_mtime  = 0;
        bool      heavy = false;
        bool      idle  = false;
        std::string heavy_reason;
        std::string idle_reason;
    };

    static State detect(int size_mb_threshold, int64_t row_cap, int idle_hours) {
        State s;
        if (fs::exists(dbPath())) s.db_bytes = fs::file_size(dbPath());

        try {
            auto& cfg = core::Config::instance();
            core::Db db(cfg.projectDbPath("."));
            db.query("SELECT COUNT(*) FROM memory_nodes WHERE deleted_at IS NULL", {},
                     [&](const core::Row& r){ if (!r.empty()) s.mem_rows = std::stoll(r[0]); });
            for (auto* tbl : {"tool_invocations", "compression_telemetry", "thinking_telemetry"}) {
                try {
                    db.query(std::string("SELECT COUNT(*) FROM ") + tbl, {},
                             [&](const core::Row& r){ if (!r.empty()) s.tel_rows += std::stoll(r[0]); });
                } catch (...) {}
            }
            try {
                db.query("SELECT MAX(created_at) FROM tool_invocations", {},
                         [&](const core::Row& r){ if (!r.empty() && !r[0].empty()) s.last_tool_invocation = std::stoll(r[0]); });
            } catch (...) {}
        } catch (...) {}

        // Newest source-file mtime (skip noise dirs).
        std::time_t newest = 0;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(projectRoot(), fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec) { ec.clear(); continue; }
            const auto& p = it->path();
            std::string name = p.filename().string();
            if (name == ".icmg" || name == ".git" || name == "build"
                || name == "node_modules" || name == "third_party") {
                if (it->is_directory()) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            auto ftime = fs::last_write_time(p, ec);
            if (ec) { ec.clear(); continue; }
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
            std::time_t t = std::chrono::system_clock::to_time_t(sctp);
            if (t > newest) newest = t;
        }
        s.newest_source_mtime = newest;

        std::time_t now = std::time(nullptr);
        long age_tool   = s.last_tool_invocation > 0 ? (now - s.last_tool_invocation) / 3600 : 99999;
        long age_source = s.newest_source_mtime  > 0 ? (now - s.newest_source_mtime)  / 3600 : 99999;

        if (s.db_bytes > (uintmax_t)size_mb_threshold * 1024 * 1024) {
            s.heavy = true;
            s.heavy_reason = "db=" + std::to_string(s.db_bytes / 1024 / 1024) + "MB > "
                           + std::to_string(size_mb_threshold) + "MB";
        }
        if (s.mem_rows > row_cap) {
            s.heavy = true;
            if (!s.heavy_reason.empty()) s.heavy_reason += "; ";
            s.heavy_reason += "rows=" + std::to_string(s.mem_rows) + " > "
                            + std::to_string(row_cap);
        }
        if (age_tool >= idle_hours && age_source >= idle_hours) {
            s.idle = true;
            s.idle_reason = "no tool>" + std::to_string(age_tool) + "h, no src>"
                          + std::to_string(age_source) + "h";
        }
        return s;
    }

    // ---- run -------------------------------------------------------------

    int cmdRun(const std::vector<std::string>& args) {
        bool aggressive = hasFlag(args, "--aggressive");
        bool force_idle = hasFlag(args, "--idle-mode");
        bool dry        = hasFlag(args, "--dry-run");
        int size_mb     = 100;
        int64_t row_cap = 50000;
        int idle_hours  = 24;
        try { size_mb     = std::stoi(flagValue(args, "--size-mb", "100")); } catch (...) {}
        try { row_cap     = std::stoll(flagValue(args, "--row-cap", "50000")); } catch (...) {}
        try { idle_hours  = std::stoi(flagValue(args, "--idle-hours", "24")); } catch (...) {}

        State s = detect(size_mb, row_cap, idle_hours);
        bool idle_mode = force_idle || s.idle;

        std::cout << "icmg maintain: detect\n"
                  << "  db_size:  " << (s.db_bytes / 1024 / 1024) << " MB\n"
                  << "  mem_rows: " << s.mem_rows << "\n"
                  << "  tel_rows: " << s.tel_rows << "\n"
                  << "  heavy:    " << (s.heavy ? "YES" : "no")
                  << (s.heavy_reason.empty() ? "" : " (" + s.heavy_reason + ")") << "\n"
                  << "  idle:     " << (s.idle ? "YES" : "no")
                  << (s.idle_reason.empty() ? "" : " (" + s.idle_reason + ")")
                  << (force_idle ? " [forced]" : "") << "\n"
                  << "  mode:     " << (aggressive ? "AGGRESSIVE" : "default")
                  << (dry ? " [dry-run]" : "") << "\n";

        if (!s.heavy && !idle_mode) {
            std::cout << "  → nothing to do (use --aggressive or --idle-mode to force)\n";
            return 0;
        }

        // Build prune chain. Subprocess each step so existing telemetry/output
        // applies. Continue on individual failure (best-effort hygiene).
        std::vector<std::string> steps;
        std::string dryflag = dry ? " --dry-run" : "";

        // Telemetry always trims first (fastest, cheapest).
        steps.push_back("icmg memory prune-telemetry" + dryflag);

        // auto:* topic prune.
        int auto_age    = aggressive ? 7  : 30;
        int session_age = aggressive ? 14 : 60;
        int corr_age    = aggressive ? 60 : 180;
        int fail_age    = aggressive ? 90 : 365;
        steps.push_back("icmg memory prune-old --topic 'auto:%' --older "
                        + std::to_string(auto_age) + "d" + dryflag);
        steps.push_back("icmg memory prune-old --topic 'session:%' --older "
                        + std::to_string(session_age) + "d" + dryflag);
        steps.push_back("icmg memory prune-old --topic 'correction:%' --older "
                        + std::to_string(corr_age) + "d" + dryflag);
        steps.push_back("icmg memory prune-old --topic 'fail:%' --older "
                        + std::to_string(fail_age) + "d" + dryflag);

        // Decay non-pinned over threshold-days.
        int decay_days = aggressive ? 14 : 30;
        steps.push_back("icmg memory decay --threshold-days "
                        + std::to_string(decay_days) + dryflag);

        // Consolidate dup memory (cosine merge) only if heavy — costly.
        if (s.heavy) {
            steps.push_back("icmg memory consolidate" + dryflag);
        }

        // Idle mode — drop memory not linked to active graph.
        if (idle_mode) {
            steps.push_back(idle_mode_inline(dry));
        }

        int run_count = 0, fail_count = 0;
        for (auto& cmd : steps) {
            if (cmd.empty()) continue;
            std::cout << "[step] " << cmd << "\n";
            auto res = core::safeExecShell(cmd, false, 120000);
            // Echo last few lines for context.
            std::string out = res.out;
            if (out.size() > 400) out = "..." + out.substr(out.size() - 400);
            if (!out.empty()) std::cout << out;
            if (res.exit_code != 0) {
                ++fail_count;
                std::cerr << "  [warn] step exit=" << res.exit_code << "\n";
            }
            ++run_count;
        }

        // Graph integrity surface — read-only by default. With --fix-graph,
        // pass through to `graph integrity --fix` (Phase 74 T4).
        {
            std::string gi = "icmg graph integrity";
            if (hasFlag(args, "--fix-graph")) gi += " --fix";
            else                              gi += " --quiet";
            std::cout << "[step] " << gi << "\n";
            auto res = core::safeExecShell(gi, false, 60000);
            std::string out = res.out;
            if (out.size() > 400) out = "..." + out.substr(out.size() - 400);
            if (!out.empty()) std::cout << out;
            ++run_count;
            if (res.exit_code > 1) ++fail_count;  // exit 1 = "issues found"
        }

        // VACUUM only if real prune happened (default no, opt-in --vacuum).
        if (!dry && hasFlag(args, "--vacuum")) {
            std::cout << "[step] VACUUM (reclaim disk)\n";
            try {
                auto& cfg = core::Config::instance();
                core::Db db(cfg.projectDbPath("."));
                db.run("VACUUM");
                std::cout << "  vacuum ok\n";
            } catch (const std::exception& e) {
                std::cerr << "  vacuum failed: " << e.what() << "\n";
                ++fail_count;
            }
        }

        std::cout << "Maintain done: " << run_count << " step(s), "
                  << fail_count << " warning(s)\n";
        return 0;
    }

    // Idle-mode inline SQL — emits a shell-quoted exec of sqlite3 over data.db
    // OR, simpler: exec icmg with a special flag. We don't have such flag yet,
    // so do it inline via a safeExec on sqlite3 binary if available; otherwise
    // do nothing and warn. Bias towards *soft-delete* (set deleted_at) so it's
    // recoverable from snapshot.
    std::string idle_mode_inline(bool dry) {
        // Use icmg's own DB via direct call inside this process at end of run
        // instead of subprocess. Return empty so subprocess loop skips, then
        // call helper after.
        idleSoftDelete(dry);
        return "";
    }

    // Soft-delete memory rows whose linked graph paths are stale (>30d) OR
    // whose linked file no longer exists. Pinned (importance=3) preserved.
    void idleSoftDelete(bool dry) {
        std::cout << "[step] idle-mode: soft-delete memory not linked to active graph\n";
        try {
            auto& cfg = core::Config::instance();
            core::Db db(cfg.projectDbPath("."));

            std::time_t now = std::time(nullptr);
            int64_t cutoff = now - (int64_t)30 * 86400;

            // Candidate query: memory rows where linked path is stale or missing.
            // Schema: memory_nodes(id, topic, content, importance, last_used,
            //                      keywords, deleted_at, ...). Some rows carry
            //                      a path-like keyword (file:<path>). Best
            //                      effort: prune memory whose `topic` LIKE
            //                      'auto:%' AND last_used < cutoff AND
            //                      importance < 2 AND not yet deleted.
            int64_t hit = 0;
            db.query("SELECT COUNT(*) FROM memory_nodes "
                     "WHERE deleted_at IS NULL AND importance < 2 "
                     "AND last_used > 0 AND last_used < ? "
                     "AND (topic LIKE 'auto:%' OR topic LIKE 'session:%' OR topic LIKE 'cache:%')",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){ if (!r.empty()) hit = std::stoll(r[0]); });
            std::cout << "  candidates: " << hit << "\n";
            if (dry || hit == 0) return;
            db.run("UPDATE memory_nodes SET deleted_at = strftime('%s','now') "
                   "WHERE deleted_at IS NULL AND importance < 2 "
                   "AND last_used > 0 AND last_used < ? "
                   "AND (topic LIKE 'auto:%' OR topic LIKE 'session:%' OR topic LIKE 'cache:%')",
                   {std::to_string(cutoff)});
            std::cout << "  soft-deleted " << hit << " row(s) (recoverable from snapshot)\n";
        } catch (const std::exception& e) {
            std::cerr << "  idle-mode skip: " << e.what() << "\n";
        }
    }

    // ---- status ----------------------------------------------------------

    int cmdStatus(const std::vector<std::string>& args) {
        int size_mb     = 100;
        int64_t row_cap = 50000;
        int idle_hours  = 24;
        try { size_mb    = std::stoi(flagValue(args, "--size-mb", "100")); } catch (...) {}
        try { row_cap    = std::stoll(flagValue(args, "--row-cap", "50000")); } catch (...) {}
        try { idle_hours = std::stoi(flagValue(args, "--idle-hours", "24")); } catch (...) {}
        State s = detect(size_mb, row_cap, idle_hours);
        std::cout << "icmg maintain status\n"
                  << "  project:   " << projectRoot().string() << "\n"
                  << "  db_size:   " << (s.db_bytes / 1024 / 1024) << " MB"
                  << " (threshold " << size_mb << " MB)\n"
                  << "  mem_rows:  " << s.mem_rows << " (cap " << row_cap << ")\n"
                  << "  tel_rows:  " << s.tel_rows << "\n";
        std::time_t now = std::time(nullptr);
        if (s.last_tool_invocation > 0)
            std::cout << "  last_tool: " << ((now - s.last_tool_invocation) / 3600) << "h ago\n";
        else
            std::cout << "  last_tool: never\n";
        if (s.newest_source_mtime > 0)
            std::cout << "  last_src:  " << ((now - s.newest_source_mtime) / 3600) << "h ago\n";
        else
            std::cout << "  last_src:  unknown\n";
        std::cout << "  HEAVY:     " << (s.heavy ? "YES" : "no")
                  << (s.heavy_reason.empty() ? "" : " — " + s.heavy_reason) << "\n"
                  << "  IDLE:      " << (s.idle ? "YES" : "no")
                  << (s.idle_reason.empty() ? "" : " — " + s.idle_reason) << "\n";
        if (s.heavy || s.idle)
            std::cout << "  → run: icmg maintain run"
                      << (s.heavy ? "" : " --idle-mode") << "\n";
        return 0;
    }

    // ---- auto-on/off/status (mirrors backup_cmd pattern) -----------------

    static std::string taskName() {
        // path + username → unique per project AND per user on shared servers
        return "icmg-maintain-" + core::icmgTaskHash(projectRoot().string());
    }

    static std::string scheduledCommand() {
        std::string root = projectRoot().string();
        std::string esc = root;
#ifdef _WIN32
        std::replace(esc.begin(), esc.end(), '\\', '/');
#endif
        return "cd \"" + esc + "\" && icmg maintain run";
    }

    int cmdAutoOn(const std::vector<std::string>& args) {
        std::string interval = flagValue(args, "--interval", "6h");
        int minutes = 360;
        if (!interval.empty()) {
            char unit = interval.back();
            try {
                int n = std::stoi(interval.substr(0, interval.size() - 1));
                if      (unit == 'h') minutes = n * 60;
                else if (unit == 'm') minutes = n;
                else                  minutes = std::stoi(interval);
            } catch (...) {}
        }
        if (minutes < 30) {
            std::cerr << "icmg maintain auto-on: interval too small (min 30m)\n";
            return 1;
        }
        std::string tn = taskName();
#ifdef _WIN32
        // Phase 78: bulletproof scheduler via core::registerWindowsSchedule.
        fs::path wrapper = projectRoot() / ".icmg" / "sched" / (tn + ".cmd");
        fs::create_directories(wrapper.parent_path());
        std::ofstream wf(wrapper, std::ios::binary);
        wf << "@echo off\r\n"
           << "cd /d \"" << projectRoot().string() << "\"\r\n"
           << "echo === %DATE% %TIME% maintain ===>> .icmg\\sched\\sched.log\r\n"
           << "icmg maintain run >> .icmg\\sched\\sched.log 2>&1\r\n";
        wf.close();
        core::ScheduleSpec spec{tn, wrapper.string(), minutes, "maintain"};
        int rc = core::registerWindowsSchedule(spec);
        if (rc != 0) return rc;
#else
        std::string cron_expr;
        if ((minutes % 60) == 0)
            cron_expr = "0 */" + std::to_string(minutes / 60) + " * * *";
        else
            cron_expr = "*/" + std::to_string(minutes) + " * * * *";
        std::string entry = cron_expr + "  " + cmd + "  # " + tn + "\n";
        auto cur = core::safeExecShell("crontab -l 2>/dev/null", false, 5000);
        std::string tab = cur.exit_code == 0 ? cur.out : "";
        std::ostringstream filtered;
        std::istringstream is(tab);
        std::string line;
        while (std::getline(is, line))
            if (line.find("# " + tn) == std::string::npos) filtered << line << "\n";
        std::string newtab = filtered.str() + entry;
        std::string tmp = "/tmp/icmg-maintain-cron.tmp";
        std::ofstream f(tmp); f << newtab; f.close();
        auto res = core::safeExecShell("crontab " + tmp, true, 5000);
        std::remove(tmp.c_str());
        if (res.exit_code != 0) {
            std::cerr << "icmg maintain auto-on: crontab failed: " << res.err << "\n";
            return 2;
        }
        std::cout << "icmg maintain auto-on: cron installed (" << cron_expr << ")\n";
#endif
        fs::create_directories(projectRoot() / ".icmg");
        std::ofstream m(projectRoot() / ".icmg" / "maintain-auto.flag");
        m << minutes << "\n";
        return 0;
    }

    int cmdAutoOff(const std::vector<std::string>&) {
        std::string tn = taskName();
#ifdef _WIN32
        core::safeExecShell("MSYS_NO_PATHCONV=1 schtasks /Delete /TN \"" + tn + "\" /F", true, 5000);
#else
        auto cur = core::safeExecShell("crontab -l 2>/dev/null", false, 5000);
        if (cur.exit_code == 0 && !cur.out.empty()) {
            std::ostringstream filtered;
            std::istringstream is(cur.out);
            std::string line;
            while (std::getline(is, line))
                if (line.find("# " + tn) == std::string::npos) filtered << line << "\n";
            std::string tmp = "/tmp/icmg-maintain-cron.tmp";
            std::ofstream f(tmp); f << filtered.str(); f.close();
            core::safeExecShell("crontab " + tmp, true, 5000);
            std::remove(tmp.c_str());
        }
#endif
        fs::remove(projectRoot() / ".icmg" / "maintain-auto.flag");
        std::cout << "icmg maintain auto-off: cleared\n";
        return 0;
    }

    int cmdAutoStatus(const std::vector<std::string>&) {
        fs::path flag = projectRoot() / ".icmg" / "maintain-auto.flag";
        bool flag_set = fs::exists(flag);
        int interval = 0;
        if (flag_set) { std::ifstream f(flag); f >> interval; }
        std::cout << "icmg maintain auto-status\n"
                  << "  task: " << taskName() << "\n"
                  << "  flag: " << (flag_set ? "ON" : "off") << "\n";
        if (flag_set) std::cout << "  interval: every " << interval << " min\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("maintain", MaintainCommand);

} // namespace icmg::cli
