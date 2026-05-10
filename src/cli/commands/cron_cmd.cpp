// Phase 72 T8: `icmg cron` — emit + install scheduler config for routine
// memory hygiene (prune-old auto-grown topics, prune-telemetry stale rows).
//
// Cross-platform: Windows Task Scheduler (schtasks), POSIX cron tab.
// Schedule: weekly Sunday 03:00 by default. User opt-in install.
//
// Tasks bundled:
//   1. memory prune-old --topic 'auto:%' --older 60d
//   2. memory prune-old --topic 'session:%' --older 90d
//   3. memory prune-old --topic 'fail:%' --older 365d
//   4. memory prune-old --topic 'correction:%' --older 180d
//   5. memory prune-telemetry (default 90d)

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace icmg::cli {

class CronCommand : public BaseCommand {
public:
    std::string name()        const override { return "cron"; }
    std::string description() const override {
        return "Emit/install scheduler config for routine memory hygiene";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg cron <action> [options]\n\n"
            "Actions:\n"
            "  show              Print platform-specific schedule snippet\n"
            "  install           Install weekly task (Sun 03:00) — Windows schtasks / cron\n"
            "  uninstall         Remove scheduled task\n"
            "  run-now           Execute hygiene tasks immediately (no scheduler)\n\n"
            "Options:\n"
            "  --schedule STR    Custom cron expression (POSIX) or `MO,TU,...` (Win)\n"
            "  --time HH:MM      Default 03:00\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];

        if (action == "show")      return doShow(args);
        if (action == "install")   return doInstall(args);
        if (action == "uninstall") return doUninstall(args);
        if (action == "run-now")   return doRunNow();

        std::cerr << "icmg cron: unknown action '" << action << "'\n";
        usage();
        return 1;
    }

private:
    // Tasks executed by the schedule — printable + executable as bash one-liner.
    static std::string hygieneCommand() {
        return
            "icmg memory prune-old --topic 'auto:%' --older 60d && "
            "icmg memory prune-old --topic 'session:%' --older 90d && "
            "icmg memory prune-old --topic 'fail:%' --older 365d && "
            "icmg memory prune-old --topic 'correction:%' --older 180d && "
            "icmg memory prune-telemetry";
    }

    int doShow(const std::vector<std::string>& args) {
        std::string time = flagValue(args, "--time", "03:00");
#ifdef _WIN32
        std::cout << "Windows Task Scheduler (schtasks) — weekly Sun " << time << "Z:\n\n";
        std::cout << "  schtasks /Create /SC WEEKLY /D SUN /ST " << time
                  << " /TN \"icmg-hygiene\" /TR \"bash -lc '"
                  << hygieneCommand() << "'\" /F\n\n";
        std::cout << "Or use `icmg cron install` to apply.\n";
#else
        std::cout << "POSIX crontab — weekly Sun " << time << ":\n\n";
        size_t colon = time.find(':');
        std::string mm = colon != std::string::npos ? time.substr(colon + 1) : "0";
        std::string hh = colon != std::string::npos ? time.substr(0, colon) : "3";
        std::cout << "  " << mm << " " << hh << " * * 0  "
                  << hygieneCommand() << "\n\n";
        std::cout << "Or use `icmg cron install` to apply.\n";
#endif
        return 0;
    }

    int doInstall(const std::vector<std::string>& args) {
        std::string time = flagValue(args, "--time", "03:00");
#ifdef _WIN32
        std::string cmd = "schtasks /Create /SC WEEKLY /D SUN /ST " + time
                        + " /TN \"icmg-hygiene\" /TR \"bash -lc '"
                        + hygieneCommand() + "'\" /F";
        auto res = core::safeExecShell(cmd, true, 15000);
        if (res.exit_code != 0) {
            std::cerr << "icmg cron install failed: " << res.err << "\n";
            return 1;
        }
        std::cout << "icmg cron: installed Windows scheduled task 'icmg-hygiene'\n"
                  << "  Schedule: weekly Sun " << time << "\n"
                  << "  Verify:  schtasks /Query /TN icmg-hygiene\n";
#else
        // Append crontab line if not present.
        size_t colon = time.find(':');
        std::string mm = colon != std::string::npos ? time.substr(colon + 1) : "0";
        std::string hh = colon != std::string::npos ? time.substr(0, colon) : "3";
        std::string entry = mm + " " + hh + " * * 0  " + hygieneCommand()
                          + "  # icmg-hygiene\n";
        // Read existing tab, append, reinstall.
        auto cur = core::safeExecShell("crontab -l 2>/dev/null", false, 5000);
        std::string tab = cur.exit_code == 0 ? cur.out : "";
        if (tab.find("# icmg-hygiene") != std::string::npos) {
            std::cout << "icmg cron: already installed (line tagged # icmg-hygiene)\n";
            return 0;
        }
        tab += entry;
        // Write to temp + crontab <file.
        std::string tmp = "/tmp/icmg-cron.tmp";
        std::ofstream f(tmp); f << tab; f.close();
        auto res = core::safeExecShell("crontab " + tmp, true, 5000);
        std::remove(tmp.c_str());
        if (res.exit_code != 0) {
            std::cerr << "icmg cron install failed: " << res.err << "\n";
            return 1;
        }
        std::cout << "icmg cron: installed crontab entry\n"
                  << "  Schedule: weekly Sun " << time << "\n"
                  << "  Verify:   crontab -l | grep icmg-hygiene\n";
#endif
        return 0;
    }

    int doUninstall(const std::vector<std::string>& /*args*/) {
#ifdef _WIN32
        auto res = core::safeExecShell(
            "schtasks /Delete /TN icmg-hygiene /F", true, 5000);
        if (res.exit_code != 0) {
            std::cerr << "icmg cron uninstall: not found or failed\n";
            return 1;
        }
        std::cout << "icmg cron: removed Windows scheduled task 'icmg-hygiene'\n";
#else
        auto cur = core::safeExecShell("crontab -l 2>/dev/null", false, 5000);
        if (cur.exit_code != 0 || cur.out.empty()) {
            std::cerr << "icmg cron uninstall: no crontab\n";
            return 1;
        }
        std::ostringstream out;
        std::istringstream is(cur.out);
        std::string line;
        bool found = false;
        while (std::getline(is, line)) {
            if (line.find("# icmg-hygiene") != std::string::npos) { found = true; continue; }
            out << line << "\n";
        }
        if (!found) {
            std::cout << "icmg cron uninstall: entry not found\n";
            return 1;
        }
        std::string tmp = "/tmp/icmg-cron.tmp";
        std::ofstream f(tmp); f << out.str(); f.close();
        auto res = core::safeExecShell("crontab " + tmp, true, 5000);
        std::remove(tmp.c_str());
        if (res.exit_code != 0) { std::cerr << "uninstall failed\n"; return 1; }
        std::cout << "icmg cron: removed crontab entry\n";
#endif
        return 0;
    }

    int doRunNow() {
        std::cout << "icmg cron: running hygiene tasks now...\n";
        auto res = core::safeExecShell(hygieneCommand(), false, 60000);
        if (res.exit_code != 0) {
            std::cerr << "icmg cron run-now: exit " << res.exit_code
                      << "\n" << res.err;
            return res.exit_code;
        }
        std::cout << res.out;
        std::cout << "icmg cron: hygiene complete.\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("cron", CronCommand);

} // namespace icmg::cli
