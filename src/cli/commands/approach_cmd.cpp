// `icmg approach` — track which task approaches succeeded vs failed.
//
// Prevents AI from repeating proven-failed paths or skipping proven-success ones.
//
// Subcommands:
//   record <task> <approach> --outcome <success|fail|partial> [--why <reason>]
//       INSERT a row. session_id from env ICMG_SESSION_ID (empty if unset).
//   lookup <task> [--limit N] [--json]
//       SELECT rows for task; ordered success → partial → fail, then created_at DESC.
//       Fail-soft: rc=0 even if DB missing or no rows.
//   list [--proven] [--task <pattern>] [--json]
//       SELECT all rows (or filtered). --proven = outcome='success' only.
//       Fail-soft: rc=0 even if DB missing.
//   prune --older-than <days>
//       DELETE rows older than N days. Fail-hard: rc=1 with stderr on bad args.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/migrator.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>

using nlohmann::json;

namespace icmg::cli {

class ApproachCommand : public BaseCommand {
public:
    std::string name()        const override { return "approach"; }
    std::string description() const override {
        return "Track task approach outcomes (success/fail/partial) to prevent repeated mistakes";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg approach <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  record <task> <approach> --outcome <success|fail|partial>\n"
            "                           [--why <reason>]\n"
            "      Record an approach outcome for a task\n\n"
            "  lookup <task> [--limit N] [--json]\n"
            "      Show recorded approaches for a task\n\n"
            "  list [--proven] [--task <pattern>] [--json]\n"
            "      List all recorded approaches (--proven = success only)\n\n"
            "  prune --older-than <days>\n"
            "      Delete rows older than N days\n\n"
            "Environment:\n"
            "  ICMG_SESSION_ID     Session identifier (used for record)\n\n"
            "Examples:\n"
            "  icmg approach record \"fix auth bug\" \"use refresh-token\" --outcome fail --why \"race condition\"\n"
            "  icmg approach record \"build speed\" \"unity build\" --outcome success\n"
            "  icmg approach lookup \"fix auth bug\"\n"
            "  icmg approach list --proven --json\n"
            "  icmg approach prune --older-than 90\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage();
            return args.empty() ? 1 : 0;
        }

        const std::string& sub = args[0];

        // Validate subcommand early.
        static const char* kKnownSubs[] = {
            "record", "lookup", "list", "prune", nullptr
        };
        bool known = false;
        for (int i = 0; kKnownSubs[i]; ++i) {
            if (sub == kKnownSubs[i]) { known = true; break; }
        }
        if (!known) {
            std::cerr << "icmg approach: unknown subcommand '" << sub << "'\n";
            std::cerr << "Run `icmg approach --help` for usage.\n";
            return 1;
        }

        // ---- record --------------------------------------------------------
        if (sub == "record") {
            // Need: task (args[1]), approach (args[2]), --outcome <value>
            if (args.size() < 3) {
                std::cerr << "icmg approach record: requires <task> <approach> --outcome <value>\n";
                return 1;
            }
            const std::string& task     = args[1];
            const std::string& approach = args[2];
            std::string outcome = getFlagValue(args, "--outcome");
            if (outcome.empty()) {
                std::cerr << "icmg approach record: --outcome <success|fail|partial> is required\n";
                return 1;
            }
            if (outcome != "success" && outcome != "fail" && outcome != "partial") {
                std::cerr << "icmg approach record: --outcome must be success, fail, or partial\n";
                return 1;
            }
            std::string why = getFlagValue(args, "--why");

            // Get session_id from env.
            std::string session_id;
            const char* env_sid = std::getenv("ICMG_SESSION_ID");
            if (env_sid && env_sid[0] != '\0') session_id = env_sid;

            // Open DB — fail-hard if not available.
            auto& cfg = core::Config::instance();
            try {
                core::Db db(cfg.projectDbPath("."));
                core::Migrator mig("__nonexistent_migrations_dir__");
                mig.runAll(db);

                db.run(
                    "INSERT INTO approaches (task, approach, outcome, why, session_id) "
                    "VALUES (?, ?, ?, ?, ?)",
                    {task, approach, outcome, why, session_id}
                );
                int64_t id = db.lastInsertId();
                std::cout << "approach: recorded #" << id
                          << " [" << outcome << "] " << task << ": " << approach << "\n";
            } catch (const std::exception& e) {
                std::cerr << "icmg approach record: " << e.what() << "\n";
                return 1;
            }
            return 0;
        }

        // ---- lookup --------------------------------------------------------
        if (sub == "lookup") {
            if (args.size() < 2) {
                std::cerr << "icmg approach lookup: missing <task>\n";
                return 1;
            }
            const std::string& task = args[1];
            bool json_mode = hasFlag(args, "--json");
            int limit = 5;
            std::string lim_str = getFlagValue(args, "--limit");
            if (!lim_str.empty()) {
                try { limit = std::stoi(lim_str); } catch (...) {}
            }

            // Fail-soft: catch all DB errors.
            auto& cfg = core::Config::instance();
            try {
                core::Db db(cfg.projectDbPath("."));
                core::Migrator mig("__nonexistent_migrations_dir__");
                mig.runAll(db);

                // Order: success first, then partial, then fail; newest first within group.
                std::string sql =
                    "SELECT id, task, approach, outcome, why, created_at, session_id "
                    "FROM approaches WHERE task = ? "
                    "ORDER BY CASE outcome WHEN 'success' THEN 0 WHEN 'partial' THEN 1 ELSE 2 END, "
                    "created_at DESC LIMIT ?";

                if (json_mode) {
                    json arr = json::array();
                    db.query(sql, {task, std::to_string(limit)},
                             [&](const core::Row& row) {
                                 json obj;
                                 obj["id"]         = row.size() > 0 ? row[0] : "";
                                 obj["task"]       = row.size() > 1 ? row[1] : "";
                                 obj["approach"]   = row.size() > 2 ? row[2] : "";
                                 obj["outcome"]    = row.size() > 3 ? row[3] : "";
                                 obj["why"]        = row.size() > 4 ? row[4] : "";
                                 obj["created_at"] = row.size() > 5 ? row[5] : "";
                                 obj["session_id"] = row.size() > 6 ? row[6] : "";
                                 arr.push_back(obj);
                             });
                    std::cout << arr.dump(2) << "\n";
                } else {
                    db.query(sql, {task, std::to_string(limit)},
                             [&](const core::Row& row) {
                                 std::string id        = row.size() > 0 ? row[0] : "?";
                                 std::string approach  = row.size() > 2 ? row[2] : "";
                                 std::string outcome   = row.size() > 3 ? row[3] : "";
                                 std::string why       = row.size() > 4 ? row[4] : "";
                                 std::cout << "#" << id << " [" << outcome << "] " << approach;
                                 if (!why.empty()) std::cout << " — " << why;
                                 std::cout << "\n";
                             });
                }
            } catch (...) {
                // Fail-soft: no DB or no rows → rc=0 silently.
            }
            return 0;
        }

        // ---- list ----------------------------------------------------------
        if (sub == "list") {
            bool json_mode = hasFlag(args, "--json");
            bool proven    = hasFlag(args, "--proven");
            std::string task_pattern = getFlagValue(args, "--task");

            auto& cfg = core::Config::instance();
            try {
                core::Db db(cfg.projectDbPath("."));
                core::Migrator mig("__nonexistent_migrations_dir__");
                mig.runAll(db);

                std::string sql =
                    "SELECT id, task, approach, outcome, why, created_at, session_id "
                    "FROM approaches WHERE 1=1";
                std::vector<std::string> params;

                if (proven) {
                    sql += " AND outcome = 'success'";
                }
                if (!task_pattern.empty()) {
                    sql += " AND task LIKE ?";
                    params.push_back("%" + task_pattern + "%");
                }
                sql += " ORDER BY task, CASE outcome WHEN 'success' THEN 0 WHEN 'partial' THEN 1 ELSE 2 END, created_at DESC";

                if (json_mode) {
                    json arr = json::array();
                    db.query(sql, params, [&](const core::Row& row) {
                        json obj;
                        obj["id"]         = row.size() > 0 ? row[0] : "";
                        obj["task"]       = row.size() > 1 ? row[1] : "";
                        obj["approach"]   = row.size() > 2 ? row[2] : "";
                        obj["outcome"]    = row.size() > 3 ? row[3] : "";
                        obj["why"]        = row.size() > 4 ? row[4] : "";
                        obj["created_at"] = row.size() > 5 ? row[5] : "";
                        obj["session_id"] = row.size() > 6 ? row[6] : "";
                        arr.push_back(obj);
                    });
                    std::cout << arr.dump(2) << "\n";
                } else {
                    db.query(sql, params, [&](const core::Row& row) {
                        std::string id       = row.size() > 0 ? row[0] : "?";
                        std::string task2    = row.size() > 1 ? row[1] : "";
                        std::string approach = row.size() > 2 ? row[2] : "";
                        std::string outcome  = row.size() > 3 ? row[3] : "";
                        std::string why      = row.size() > 4 ? row[4] : "";
                        std::cout << "#" << id << " [" << outcome << "] " << task2 << ": " << approach;
                        if (!why.empty()) std::cout << " — " << why;
                        std::cout << "\n";
                    });
                }
            } catch (...) {
                // Fail-soft: rc=0 silently.
            }
            return 0;
        }

        // ---- prune ---------------------------------------------------------
        if (sub == "prune") {
            std::string days_str = getFlagValue(args, "--older-than");
            if (days_str.empty()) {
                std::cerr << "icmg approach prune: --older-than <days> is required\n";
                return 1;
            }
            int days = 0;
            try { days = std::stoi(days_str); } catch (...) {
                std::cerr << "icmg approach prune: invalid days value: " << days_str << "\n";
                return 1;
            }
            if (days <= 0) {
                std::cerr << "icmg approach prune: --older-than must be > 0\n";
                return 1;
            }

            auto& cfg = core::Config::instance();
            try {
                core::Db db(cfg.projectDbPath("."));
                core::Migrator mig("__nonexistent_migrations_dir__");
                mig.runAll(db);

                // Cutoff: now - days*86400 seconds
                std::string cutoff = std::to_string(
                    static_cast<long long>(days) * 86400LL
                );
                db.run(
                    "DELETE FROM approaches WHERE created_at < (strftime('%s','now') - ?)",
                    {cutoff}
                );
                std::cout << "approach: pruned rows older than " << days << " day(s)\n";
            } catch (const std::exception& e) {
                std::cerr << "icmg approach prune: " << e.what() << "\n";
                return 1;
            }
            return 0;
        }

        // Should not reach here.
        return 1;
    }

private:
    // Return value of a flag like --flag <value> or return "".
    static std::string getFlagValue(const std::vector<std::string>& args,
                                    const std::string& flag) {
        for (std::size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == flag) return args[i + 1];
        }
        return {};
    }
};

ICMG_REGISTER_COMMAND("approach", ApproachCommand);

} // namespace icmg::cli
