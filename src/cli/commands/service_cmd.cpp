// v1.1.0 Task 6.5: `icmg service` — resident daemon manager.
//
// Subcommands:
//   start        Spawn detached `icmg service run`
//   stop         Signal running daemon to exit
//   status       Show PID + last-tick per task
//   run          Blocking ticker loop (invoked by launcher VBS)
//   install      Schedule logon-trigger task → wscript //B //Nologo service.vbs
//   uninstall    Remove logon-trigger task
//
// One daemon per user session. Replaces 5 per-project scheduled tasks
// with 1 logon-trigger. Internal ticker runs commands in-process via
// Registry<BaseCommand>::create → zero subprocess fork → no popup flash.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/path_utils.hpp"
#include "../../core/service_loop.hpp"
#include "../../core/service_install.hpp"
#include "../../core/exec_utils.hpp"

#include <nlohmann/json.hpp>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

class ServiceCommand : public BaseCommand {
public:
    std::string name()        const override { return "service"; }
    std::string description() const override {
        return "Resident daemon: ticks backup/maintain/mirror/sentinel in-process";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg service <action>\n\n"
            "Actions:\n"
            "  start        Spawn detached daemon\n"
            "  stop         Signal running daemon to exit\n"
            "  status       Show PID + last-tick per task\n"
            "  run          Blocking ticker loop (invoked by launcher)\n"
            "  install      Schedule logon-trigger task (Windows)\n"
            "  uninstall    Remove logon-trigger task\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        if (sub == "run")       return doRun();
        if (sub == "start")     return doStart();
        if (sub == "stop")      return doStop();
        if (sub == "status")    return doStatus();
        if (sub == "install")   return doInstall();
        if (sub == "uninstall") return doUninstall();
        std::cerr << "icmg service: unknown action '" << sub << "'\n";
        return 1;
    }

private:
    int doRun() {
        core::ServiceLoop loop;
        return loop.run();
    }

    int doStart() {
        // Spawn `icmg service run` as detached background process.
#ifdef _WIN32
        std::string cmd = "start \"\" /B icmg service run";
#else
        std::string cmd = "(icmg service run &)";
#endif
        (void)core::safeExecShell(cmd, false, 2000);
        std::cout << "icmg service: spawn requested\n";
        return 0;
    }

    int doStop() {
        fs::path pidf = core::servicePidPath();
        if (!fs::exists(pidf)) {
            std::cout << "icmg service: not running (no PID file)\n";
            return 0;
        }
        std::ifstream f(pidf);
        long long pid = 0;
        f >> pid;
        if (pid <= 0) { std::cerr << "icmg service: bad PID file\n"; return 1; }
#ifdef _WIN32
        std::string cmd = "taskkill /PID " + std::to_string(pid) + " /F";
#else
        std::string cmd = "kill -TERM " + std::to_string(pid);
#endif
        (void)core::safeExecShell(cmd, false, 5000);
        std::cout << "icmg service: signaled PID " << pid << "\n";
        return 0;
    }

    int doStatus() {
        fs::path pidf = core::servicePidPath();
        if (!fs::exists(pidf)) {
            std::cout << "  running: no\n";
            return 0;
        }
        long long pid = 0;
        { std::ifstream f(pidf); f >> pid; }
        std::cout << "  running: yes\n  pid:     " << pid << "\n";

        std::ifstream sf(core::serviceStatePath());
        if (!sf) return 0;
        json j;
        try { sf >> j; } catch (...) { return 0; }
        if (!j.contains("tasks") || !j["tasks"].is_object()) return 0;
        std::cout << "  last-tick:\n";
        int64_t now = (int64_t)std::time(nullptr);
        for (auto& [k, v] : j["tasks"].items()) {
            int64_t ts = v.value("last_success", 0LL);
            std::cout << "    " << k << ": " << (ts ? std::to_string(now - ts) + "s ago" : "never") << "\n";
        }
        return 0;
    }

    int doInstall() {
        std::string err;
        bool ok = core::installResidentService(&err);
        if (!ok) {
            std::cerr << "icmg service install: " << err << "\n";
            return 1;
        }
#ifdef _WIN32
        std::cout << "icmg service: logon-trigger registered (icmg-service)\n";
#else
        std::cout << "icmg service install: POSIX no-op (systemd/launchd out of scope)\n";
#endif
        int removed = core::cleanupLegacySchtasks();
        if (removed > 0)
            std::cout << "icmg service: cleaned " << removed << " legacy schtasks\n";
        return 0;
    }

    int doUninstall() {
#ifdef _WIN32
        std::string cmd = "MSYS_NO_PATHCONV=1 schtasks /Delete /TN \"icmg-service\" /F";
        auto r = core::safeExecShell(cmd, true, 5000);
        return r.exit_code;
#else
        std::cout << "icmg service uninstall: POSIX no-op.\n";
        return 0;
#endif
    }
};

ICMG_REGISTER_COMMAND("service", ServiceCommand);

} // namespace icmg::cli
