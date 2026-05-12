// icmg rule-daemon — lifecycle management for the rule enforcement daemon.
//
// Subcommands:
//   start   — spawn daemon process (detached), write PID to ~/.icmg/rule-daemon.pid
//   stop    — send SHUTDOWN via IPC, remove PID file
//   status  — ping daemon, show PID + pipe name
//   reload  — send RELOAD (refresh rules from DB without restart)

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/exec_utils.hpp"
#include "../../daemon/rule_daemon.hpp"
#include "../../daemon/rule_daemon_client.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <string>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <signal.h>
#endif

namespace fs = std::filesystem;
using namespace icmg::daemon;

namespace icmg::cli {

static std::string pidFilePath() {
    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.icmg/rule-daemon.pid";
}

static void writePid(int pid) {
    std::string path = pidFilePath();
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    if (f) f << pid << "\n";
}

static int readPid() {
    std::ifstream f(pidFilePath());
    int pid = 0;
    if (f) f >> pid;
    return pid;
}

static void removePid() {
    std::error_code ec;
    fs::remove(pidFilePath(), ec);
}

class RuleDaemonCommand : public BaseCommand {
public:
    std::string name()        const override { return "rule-daemon"; }
    std::string description() const override { return "Rule enforcement daemon lifecycle (start/stop/status/reload)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg rule-daemon <subcommand>\n\n"
            "Subcommands:\n"
            "  start   Spawn daemon (detached). Fails if already running.\n"
            "  stop    Send SHUTDOWN to daemon, remove PID file.\n"
            "  status  Ping daemon, show PID and pipe path.\n"
            "  reload  Refresh rules from DB without restart.\n"
            "  strict [on|off|status]  Toggle strict mode (block ALL reads).\n"
            "  run     Run daemon in foreground (used internally by start).\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];

        if (sub == "run") {
            // Foreground run — called by `start` in detached process
            auto& cfg = core::Config::instance();
            RuleDaemon daemon(cfg.projectDbPath("."));
            return daemon.run();
        }

        if (sub == "start") {
            if (RuleDaemonClient::ping()) {
                std::cout << "rule-daemon: already running (pid=" << readPid() << ")\n";
                return 0;
            }
#ifdef _WIN32
            // Spawn detached on Windows via CREATE_NEW_CONSOLE + DETACHED_PROCESS
            STARTUPINFOA si{};
            PROCESS_INFORMATION pi{};
            si.cb = sizeof(si);
            std::string cmd = "icmg rule-daemon run";
            if (CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                               nullptr, nullptr, &si, &pi)) {
                writePid((int)pi.dwProcessId);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                std::cout << "rule-daemon: started (pid=" << pi.dwProcessId << ")\n";
                std::cout << "pipe: " << RuleDaemon::pipeName() << "\n";
                return 0;
            }
            std::cerr << "rule-daemon: failed to start (CreateProcess error)\n";
            return 1;
#else
            pid_t pid = fork();
            if (pid < 0) { std::cerr << "rule-daemon: fork failed\n"; return 1; }
            if (pid == 0) {
                setsid();
                execlp("icmg", "icmg", "rule-daemon", "run", nullptr);
                _exit(1);
            }
            writePid(pid);
            std::cout << "rule-daemon: started (pid=" << pid << ")\n";
            std::cout << "socket: " << RuleDaemon::pipeName() << "\n";
            return 0;
#endif
        }

        if (sub == "stop") {
            if (!RuleDaemonClient::ping()) {
                std::cout << "rule-daemon: not running\n";
                removePid();
                return 0;
            }
            RuleDaemonClient::shutdown();
            removePid();
            std::cout << "rule-daemon: stopped\n";
            return 0;
        }

        if (sub == "status") {
            bool alive = RuleDaemonClient::ping();
            int pid = readPid();
            std::cout << "rule-daemon status\n";
            std::cout << "  running : " << (alive ? "yes" : "no") << "\n";
            std::cout << "  pid     : " << (pid > 0 ? std::to_string(pid) : "unknown") << "\n";
            std::cout << "  pipe    : " << RuleDaemon::pipeName() << "\n";
            std::cout << "  pid-file: " << pidFilePath() << "\n";
            return alive ? 0 : 1;
        }

        if (sub == "reload") {
            if (!RuleDaemonClient::ping()) {
                std::cerr << "rule-daemon: not running\n";
                return 1;
            }
            RuleDaemonClient::reload();
            std::cout << "rule-daemon: rules reloaded\n";
            return 0;
        }

        if (sub == "strict") {
            std::string mode = args.size() > 1 ? args[1] : "status";
            if (mode == "status") {
                if (!RuleDaemonClient::ping()) {
                    std::cout << "rule-daemon: not running\n"; return 1;
                }
                bool on = RuleDaemonClient::getStrict();
                std::cout << "strict mode: " << (on ? "ON" : "OFF") << "\n";
                std::cout << "  ON  — blocks ALL Read/Glob/Grep, no size threshold\n";
                std::cout << "  OFF — blocks only files >= 500 lines (default)\n";
                return 0;
            }
            if (mode == "on" || mode == "off") {
                bool on = (mode == "on");
                if (!RuleDaemonClient::ping()) {
                    std::cerr << "rule-daemon: not running. Start with: icmg rule-daemon start\n";
                    return 1;
                }
                RuleDaemonClient::setStrict(on);
                std::cout << "rule-daemon: strict mode " << (on ? "ON" : "OFF") << "\n";
                if (on) {
                    std::cout << "  All Read/Glob/Grep calls now blocked.\n";
                    std::cout << "  Bypass per-call: set env ICMG_STRICT_BYPASS=1\n";
                    std::cout << "  Turn off: icmg rule-daemon strict off\n";
                } else {
                    std::cout << "  Back to threshold mode (warn>=200, block>=500 lines).\n";
                }
                return 0;
            }
            std::cerr << "rule-daemon strict: unknown mode '" << mode << "'. Use: on|off|status\n";
            return 1;
        }

        std::cerr << "rule-daemon: unknown subcommand '" << sub << "'. Try --help.\n";
        return 1;
    }
};

ICMG_REGISTER_COMMAND("rule-daemon", RuleDaemonCommand);

} // namespace icmg::cli
