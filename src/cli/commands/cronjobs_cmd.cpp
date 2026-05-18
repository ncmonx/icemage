// v1.6.0: `icmg cronjobs` — manage global cron_jobs table.
// Distinct from legacy `icmg cron` (memory-prune scheduler config).
// New table backs the consolidated icmg-service iterator that replaces per-
// project `icmg-{backup,maintain,mirror,sentinel,shadow-upgrade}-<hash>`
// scheduled tasks (N projects × 5 → 1 global service).
//
// Subcommands:
//   list             Print registered chores: project | chore | every | last_run
//   remove <proj>    Drop all chores for project_path
//   run-now <chore>  Fire a chore once for cwd (sync, blocks)
//   reregister       Populate cron_jobs for cwd (5 default chores)
//   sweep            Remove legacy `icmg-{...}-<hash>` Win schtasks

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/cron_store.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/schedule_helper.hpp"

#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

class CronJobsCommand : public BaseCommand {
public:
    std::string name()        const override { return "cronjobs"; }
    std::string description() const override {
        return "Manage global cron_jobs (icmg-service iterates internally)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg cronjobs <action> [args]\n\n"
            "Actions:\n"
            "  list                 Print registered chores from global.db cron_jobs\n"
            "  remove <project>     Drop all chores for project_path\n"
            "  run-now <chore...>   Fire a chore once via cwd (sync, blocks)\n"
            "  reregister           Populate cron_jobs for cwd (5 default chores)\n"
            "  sweep                Remove legacy per-project Win schtasks\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        if (sub == "list")        return cmdList();
        if (sub == "remove")      return cmdRemove(rest);
        if (sub == "run-now")     return cmdRunNow(rest);
        if (sub == "reregister")  return cmdReregister();
        if (sub == "sweep")       return cmdSweep();
        std::cerr << "icmg cronjobs: unknown action '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    int cmdList() {
        core::CronStore cs(core::Config::instance().globalDbPath());
        auto jobs = cs.listAll();
        if (jobs.empty()) {
            std::cout << "icmg cronjobs: no jobs registered.\n"
                      << "  Run `icmg init --force` to register cwd's chores.\n";
            return 0;
        }
        std::cout << std::left
                  << std::setw(50) << "project"
                  << std::setw(28) << "chore"
                  << std::setw(8)  << "every"
                  << "last_run\n";
        std::cout << std::string(110, '-') << "\n";
        int64_t now = (int64_t)std::time(nullptr);
        for (auto& j : jobs) {
            std::string last = j.last_run > 0
                ? std::to_string((now - j.last_run) / 60) + "m ago"
                : "never";
            std::cout << std::left
                      << std::setw(50) << j.project_path.substr(0, 49)
                      << std::setw(28) << j.chore.substr(0, 27)
                      << std::setw(8)  << (std::to_string(j.every_min) + "m")
                      << last << "\n";
        }
        return 0;
    }

    int cmdRemove(const std::vector<std::string>& rest) {
        if (rest.empty()) {
            std::cerr << "icmg cronjobs remove: missing <project_path>\n";
            return 1;
        }
        core::CronStore cs(core::Config::instance().globalDbPath());
        cs.removeProject(rest[0]);
        std::cout << "icmg cronjobs: removed all chores for " << rest[0] << "\n";
        return 0;
    }

    int cmdRunNow(const std::vector<std::string>& rest) {
        if (rest.empty()) {
            std::cerr << "icmg cronjobs run-now: missing <chore>\n";
            return 1;
        }
        std::string chore;
        for (auto& a : rest) {
            if (!chore.empty()) chore += " ";
            chore += a;
        }
        std::cout << "icmg cronjobs run-now: " << chore << "\n";
        std::string cmd = "icmg " + chore;
        auto res = core::safeExecShell(cmd, false, 300000);
        if (res.exit_code != 0) {
            std::cerr << "icmg cronjobs run-now: exit=" << res.exit_code << "\n";
        }
        return res.exit_code;
    }

    int cmdReregister() {
        std::error_code ec;
        fs::path cwd = fs::current_path(ec);
        if (ec) {
            std::cerr << "icmg cronjobs: cwd unavailable\n";
            return 1;
        }
        core::CronStore cs(core::Config::instance().globalDbPath());
        struct { const char* chore; int every; } defaults[] = {
            {"backup snapshot --note auto-hourly", 60},
            {"maintain run",                       360},
            {"mirror sync",                        15},
            {"sentinel run --quiet",               15},
            {"shadow-upgrade check",               1440},
        };
        for (auto& d : defaults) {
            cs.upsert(cwd.string(), d.chore, d.every);
        }
        std::cout << "icmg cronjobs: registered 5 chores for " << cwd.string() << "\n";
        return 0;
    }

    int cmdSweep() {
        int n = core::sweepLegacySchtasks();
        std::cout << "icmg cronjobs sweep: removed " << n
                  << " legacy schtasks.\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("cronjobs", CronJobsCommand);

}  // namespace icmg::cli
