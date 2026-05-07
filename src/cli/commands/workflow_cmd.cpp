// Phase 22: workflow integration commands.
//   icmg known-issue add/match/list/stats
//   icmg verify --record / show / gate
//   icmg phase list/show/start/verify/ship
//   icmg design register/approve/check/list
//   icmg log save/search/show/recent
//
// All reuse memory_nodes via reserved topic prefixes plus dedicated tables
// (verifications, phases, designs) for structured fields.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"
#include "../../core/exec_utils.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <cstdint>
#include <algorithm>

namespace icmg::cli {

static int64_t wfNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::string fnv64hex(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

// =============================================================================
// known-issue
// =============================================================================

class KnownIssueCommand : public BaseCommand {
public:
    std::string name()        const override { return "known-issue"; }
    std::string description() const override { return "Recurring error / past resolution registry"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg known-issue <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  add <pattern> --fix <description> [--zone Z]   Register a fix\n"
            "  match <error-text>                              Find past resolutions\n"
            "  list [--zone Z] [--limit N]                     List all\n"
            "  stats                                            Most-recurring errors\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        if (sub == "add") {
            if (rest.empty()) { std::cerr << "icmg known-issue add: requires <pattern>\n"; return 1; }
            std::string pattern = rest[0];
            std::string fix     = flagValue(rest, "--fix");
            std::string zone    = flagValue(rest, "--zone", "default");
            if (fix.empty()) { std::cerr << "icmg known-issue add: --fix <description> required\n"; return 1; }

            imem::MemoryNode n;
            n.topic    = "errors-resolved " + pattern;
            n.content  = "Pattern: " + pattern + "\nFix: " + fix;
            n.keywords = pattern + " error fix resolved";
            n.importance = 2;  // high
            n.zone     = zone;
            try { int64_t id = mem.store(n, /*force=*/true); std::cout << "Registered #" << id << "\n"; }
            catch (const std::exception& e) { std::cerr << "store failed: " << e.what() << "\n"; return 1; }
            return 0;
        }
        if (sub == "match") {
            if (rest.empty()) { std::cerr << "icmg known-issue match: requires <error-text>\n"; return 1; }
            std::string err;
            for (auto& a : rest) { if (!err.empty()) err += " "; err += a; }
            // Topic-prefix search to scope to errors-resolved
            auto candidates = mem.recallByTopic("errors-resolved", 50);
            // Simple substring scoring against err text
            std::string err_lo = err;
            std::transform(err_lo.begin(), err_lo.end(), err_lo.begin(), ::tolower);
            int matched = 0;
            for (auto& n : candidates) {
                std::string topic_lo = n.topic;
                std::transform(topic_lo.begin(), topic_lo.end(), topic_lo.begin(), ::tolower);
                // Extract pattern (after "errors-resolved ")
                std::string pat = topic_lo.size() > 17 ? topic_lo.substr(17) : "";
                if (!pat.empty() && err_lo.find(pat) != std::string::npos) {
                    std::cout << "[match] #" << n.id << "  " << n.topic << "\n";
                    std::cout << "  " << n.content << "\n\n";
                    if (++matched >= 5) break;
                }
            }
            if (matched == 0) std::cout << "No prior resolution matches.\n";
            return 0;
        }
        if (sub == "list") {
            int limit = 50;
            try { limit = std::stoi(flagValue(rest, "--limit", "50")); } catch (...) {}
            std::string zone = flagValue(rest, "--zone");
            auto items = mem.recallByTopic("errors-resolved", limit);
            for (auto& n : items) {
                if (!zone.empty() && n.zone != zone) continue;
                std::cout << "#" << n.id << "  [" << n.zone << "]  " << n.topic << "\n";
            }
            return 0;
        }
        if (sub == "stats") {
            auto items = mem.recallByTopic("errors-resolved", 1000);
            std::cout << "=== known-issue stats ===\n"
                      << "Total: " << items.size() << "\n";
            return 0;
        }
        std::cerr << "icmg known-issue: unknown subcommand: " << sub << "\n";
        usage();
        return 1;
    }
};

// =============================================================================
// verify
// =============================================================================

class VerifyCommand : public BaseCommand {
public:
    std::string name()        const override { return "verify"; }
    std::string description() const override { return "Run verification commands and record evidence"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg verify [options]\n\n"
            "Modes:\n"
            "  --command \"<cmd>\" [--phase N]   Run command, record exit + output hash\n"
            "  show [--phase N]                 List recorded verifications\n"
            "  gate [--phase N]                 Exit 0 if all recorded pass; non-zero else\n";
    }

    int run(const std::vector<std::string>& args) override {
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        if (args.empty() || args[0] == "--help") { usage(); return 0; }

        if (args[0] == "show") {
            std::string phase = flagValue(args, "--phase");
            std::string sql = "SELECT id, phase, command, exit_code, recorded_at FROM verifications";
            std::vector<std::string> params;
            if (!phase.empty()) { sql += " WHERE phase=?"; params.push_back(phase); }
            sql += " ORDER BY recorded_at DESC LIMIT 50";
            db.query(sql, params, [&](const core::Row& r){
                if (r.size() < 5) return;
                std::cout << "#" << r[0] << "  phase=" << r[1]
                          << "  exit=" << r[3] << "  " << r[2].substr(0,80) << "\n";
            });
            return 0;
        }
        if (args[0] == "gate") {
            std::string phase = flagValue(args, "--phase");
            int total = 0, fail = 0;
            std::string sql = "SELECT exit_code FROM verifications";
            std::vector<std::string> params;
            if (!phase.empty()) { sql += " WHERE phase=?"; params.push_back(phase); }
            db.query(sql, params, [&](const core::Row& r){
                if (r.empty()) return;
                ++total;
                try { if (std::stoi(r[0]) != 0) ++fail; } catch (...) {}
            });
            if (total == 0) {
                std::cerr << "verify gate: no verifications recorded"
                          << (phase.empty() ? "" : " for phase " + phase) << "\n";
                return 2;
            }
            if (fail > 0) {
                std::cerr << "verify gate: " << fail << "/" << total << " failed\n";
                return 1;
            }
            std::cout << "verify gate: " << total << "/" << total << " passed\n";
            return 0;
        }

        // Run mode: --command <cmd> [--phase N]
        std::string command = flagValue(args, "--command");
        if (command.empty()) {
            // accept positional
            for (auto& a : args) { if (!a.empty() && a[0] != '-' && a != "show" && a != "gate") { command = a; break; } }
        }
        if (command.empty()) { std::cerr << "icmg verify: --command \"<cmd>\" required\n"; return 1; }
        std::string phase = flagValue(args, "--phase");

        auto t0 = std::chrono::steady_clock::now();
        auto result = core::safeExec({"sh", "-c", command}, /*merge_stderr=*/true, /*timeout_ms=*/600000);
        auto t1 = std::chrono::steady_clock::now();
        int dur = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        std::string hash = fnv64hex(result.out);
        std::string head = result.out.substr(0, 1024);

        db.run(
            "INSERT INTO verifications(phase, command, exit_code, output_hash, output_head, duration_ms)"
            " VALUES(?,?,?,?,?,?)",
            {phase, command, std::to_string(result.exit_code), hash, head, std::to_string(dur)});

        std::cout << "verify: exit=" << result.exit_code
                  << " duration=" << dur << "ms"
                  << " hash=" << hash.substr(0,8)
                  << (phase.empty() ? "" : " phase=" + phase)
                  << "\n";
        std::cout << result.out;
        return result.exit_code;
    }
};

// =============================================================================
// phase
// =============================================================================

class PhaseCommand : public BaseCommand {
public:
    std::string name()        const override { return "phase"; }
    std::string description() const override { return "Phase lifecycle management (GSD-style)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg phase <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  list                            Show all phases + status\n"
            "  show <num>                      Full detail\n"
            "  start <num> [--name X] [--goal Y] [--plan PATH]\n"
            "  verify <num>                    Goal-backward check (verifications must pass)\n"
            "  ship <num> [--commit SHA]       Mark completed\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        if (sub == "list") {
            db.query("SELECT num, name, status, started_at, completed_at FROM phases ORDER BY num", {},
                     [&](const core::Row& r){
                         if (r.size() < 3) return;
                         std::cout << "[" << r[2] << "]  " << r[0] << "  " << r[1] << "\n";
                     });
            return 0;
        }
        if (sub == "show") {
            if (rest.empty()) { std::cerr << "phase show: requires <num>\n"; return 1; }
            db.query("SELECT num, name, goal, plan_path, status, started_at, completed_at, commit_sha, notes FROM phases WHERE num=?",
                     {rest[0]},
                     [&](const core::Row& r){
                         if (r.size() < 9) return;
                         std::cout << "Num     : " << r[0] << "\n"
                                   << "Name    : " << r[1] << "\n"
                                   << "Goal    : " << r[2] << "\n"
                                   << "Plan    : " << r[3] << "\n"
                                   << "Status  : " << r[4] << "\n"
                                   << "Started : " << r[5] << "\n"
                                   << "Done    : " << r[6] << "\n"
                                   << "Commit  : " << r[7] << "\n"
                                   << "Notes   : " << r[8] << "\n";
                     });
            return 0;
        }
        if (sub == "start") {
            if (rest.empty()) { std::cerr << "phase start: requires <num>\n"; return 1; }
            std::string num   = rest[0];
            std::string nm    = flagValue(rest, "--name", num);
            std::string goal  = flagValue(rest, "--goal");
            std::string plan  = flagValue(rest, "--plan");
            db.run(
                "INSERT INTO phases(num, name, goal, plan_path, status, started_at) VALUES(?,?,?,?,'in-progress',?)"
                " ON CONFLICT(num) DO UPDATE SET name=excluded.name, goal=excluded.goal,"
                " plan_path=excluded.plan_path, status='in-progress', started_at=excluded.started_at",
                {num, nm, goal, plan, std::to_string(wfNow())});
            std::cout << "Started phase " << num << "\n";
            return 0;
        }
        if (sub == "verify") {
            if (rest.empty()) { std::cerr << "phase verify: requires <num>\n"; return 1; }
            std::string num = rest[0];
            // Goal-backward check: all recorded verifications for this phase must pass
            int total = 0, fail = 0;
            db.query("SELECT exit_code FROM verifications WHERE phase=?", {num},
                     [&](const core::Row& r){
                         if (r.empty()) return;
                         ++total;
                         try { if (std::stoi(r[0]) != 0) ++fail; } catch (...) {}
                     });
            std::cout << "phase " << num << " verification:\n";
            std::cout << "  recorded checks: " << total << "\n"
                      << "  failures:         " << fail << "\n";
            if (total == 0) {
                std::cout << "  VERDICT: NO-GO (no verifications recorded)\n";
                return 2;
            }
            if (fail > 0) {
                std::cout << "  VERDICT: NO-GO\n";
                return 1;
            }
            std::cout << "  VERDICT: GO\n";
            return 0;
        }
        if (sub == "ship") {
            if (rest.empty()) { std::cerr << "phase ship: requires <num>\n"; return 1; }
            std::string num    = rest[0];
            std::string commit = flagValue(rest, "--commit");
            db.run("UPDATE phases SET status='done', completed_at=?, commit_sha=? WHERE num=?",
                   {std::to_string(wfNow()), commit, num});
            std::cout << "Shipped phase " << num << "\n";
            return 0;
        }
        std::cerr << "icmg phase: unknown subcommand: " << sub << "\n";
        usage();
        return 1;
    }
};

// =============================================================================
// design
// =============================================================================

class DesignCommand : public BaseCommand {
public:
    std::string name()        const override { return "design"; }
    std::string description() const override { return "Design doc registry + brainstorming gate"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg design <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  register <feature> <doc_path>   Register draft design\n"
            "  approve <feature> [--by NAME]   Mark approved\n"
            "  check <feature>                 Exit 0 if approved, non-zero else\n"
            "  list                             Show all designs + status\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        if (sub == "register") {
            if (rest.size() < 2) { std::cerr << "design register: requires <feature> <doc_path>\n"; return 1; }
            db.run(
                "INSERT INTO designs(feature, doc_path, status) VALUES(?,?,'draft')"
                " ON CONFLICT(feature) DO UPDATE SET doc_path=excluded.doc_path",
                {rest[0], rest[1]});
            std::cout << "Registered design: " << rest[0] << " → " << rest[1] << "\n";
            return 0;
        }
        if (sub == "approve") {
            if (rest.empty()) { std::cerr << "design approve: requires <feature>\n"; return 1; }
            std::string by = flagValue(rest, "--by", "user");
            db.run("UPDATE designs SET status='approved', approved_at=?, approved_by=? WHERE feature=?",
                   {std::to_string(wfNow()), by, rest[0]});
            std::cout << "Approved: " << rest[0] << " by " << by << "\n";
            return 0;
        }
        if (sub == "check") {
            if (rest.empty()) { std::cerr << "design check: requires <feature>\n"; return 1; }
            std::string status;
            db.query("SELECT status FROM designs WHERE feature=?", {rest[0]},
                     [&](const core::Row& r){ if (!r.empty()) status = r[0]; });
            if (status == "approved") {
                std::cout << "approved\n";
                return 0;
            }
            std::cerr << "design check: " << rest[0] << " not approved (status: "
                      << (status.empty() ? "unregistered" : status) << ")\n";
            return 1;
        }
        if (sub == "list") {
            db.query("SELECT feature, doc_path, status, approved_by FROM designs ORDER BY feature", {},
                     [&](const core::Row& r){
                         if (r.size() < 4) return;
                         std::cout << "[" << r[2] << "]  " << r[0] << "  → " << r[1];
                         if (!r[3].empty()) std::cout << "  (by " << r[3] << ")";
                         std::cout << "\n";
                     });
            return 0;
        }
        std::cerr << "icmg design: unknown subcommand: " << sub << "\n";
        usage();
        return 1;
    }
};

// =============================================================================
// log (queryable session-log)
// =============================================================================

class WfLogCommand : public BaseCommand {
public:
    std::string name()        const override { return "wflog"; }  // avoid clash with future 'log'
    std::string description() const override { return "Queryable session log entries"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg wflog <subcommand>\n\n"
            "Subcommands:\n"
            "  save --goal G --decisions D [--rejected R] [--open O] [--zone Z]\n"
            "  search <query>                    BM25 over saved entries\n"
            "  recent [--limit N]                Newest first\n"
            "  show <id>                          Full detail\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        if (sub == "save") {
            std::string goal = flagValue(rest, "--goal");
            std::string dec  = flagValue(rest, "--decisions");
            std::string rej  = flagValue(rest, "--rejected");
            std::string opn  = flagValue(rest, "--open");
            std::string zone = flagValue(rest, "--zone", "default");
            if (goal.empty()) { std::cerr << "wflog save: --goal required\n"; return 1; }
            imem::MemoryNode n;
            n.topic    = "log-saved " + goal.substr(0, 40);
            n.content  = "Goal: " + goal + "\nDecisions: " + dec
                       + "\nRejected: " + rej + "\nOpen: " + opn;
            n.keywords = "log saved " + goal;
            n.importance = 2;
            n.zone = zone;
            try { int64_t id = mem.store(n, /*force=*/true); std::cout << "Saved #" << id << "\n"; }
            catch (...) { std::cerr << "save failed\n"; return 1; }
            return 0;
        }
        if (sub == "search") {
            if (rest.empty()) { std::cerr << "wflog search: requires <query>\n"; return 1; }
            std::string q;
            for (auto& a : rest) { if (!q.empty()) q += " "; q += a; }
            auto results = mem.recall(q, 10, false);
            for (auto& n : results) {
                if (n.topic.rfind("log-saved", 0) != 0) continue;
                std::cout << "[" << std::fixed << std::setprecision(1) << n.score << "] #"
                          << n.id << "  " << n.topic << "\n";
            }
            return 0;
        }
        if (sub == "recent") {
            int limit = 10;
            try { limit = std::stoi(flagValue(rest, "--limit", "10")); } catch (...) {}
            auto items = mem.recallByTopic("log-saved", limit);
            for (auto& n : items) std::cout << "#" << n.id << "  " << n.topic << "\n";
            return 0;
        }
        if (sub == "show") {
            if (rest.empty()) { std::cerr << "wflog show: requires <id>\n"; return 1; }
            int64_t id; try { id = std::stoll(rest[0]); } catch (...) { return 1; }
            auto n = mem.get(id);
            if (n.id == 0) { std::cerr << "not found\n"; return 1; }
            std::cout << "Topic: " << n.topic << "\n\n" << n.content << "\n";
            return 0;
        }
        std::cerr << "icmg wflog: unknown subcommand: " << sub << "\n";
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("known-issue", KnownIssueCommand);
ICMG_REGISTER_COMMAND("verify",      VerifyCommand);
ICMG_REGISTER_COMMAND("phase",       PhaseCommand);
ICMG_REGISTER_COMMAND("design",      DesignCommand);
ICMG_REGISTER_COMMAND("wflog",       WfLogCommand);

} // namespace icmg::cli
