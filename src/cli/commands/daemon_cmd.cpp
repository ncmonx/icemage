// Phase 80 (scaffold): `icmg daemon` — persistent server mode.
//
// Full implementation deferred — current scaffold provides cmd surface
// (start/stop/status/restart) + pidfile management. Future iteration wires
// Named Pipe (Win) / Unix socket (POSIX) listener + JSON-RPC dispatch so
// hook scripts can `icmg-client send <event>` and get response in ~5ms
// (vs 360ms cold-start fork).
//
// For now: start writes pidfile + sleeps; clients fall back to per-invocation
// mode (existing behavior). Sets up architecture; IPC wiring next phase.
//
// Subcommands:
//   start [--foreground]   Spawn daemon; write pidfile
//   stop                   SIGTERM via pidfile
//   status                 Show pidfile state + uptime
//   restart                stop + start

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/path_utils.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <signal.h>
  #include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace icmg::cli {

class DaemonCommand : public BaseCommand {
public:
    std::string name()        const override { return "daemon"; }
    std::string description() const override {
        return "Persistent server mode (scaffold; full IPC in next phase)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg daemon <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  start [--foreground]   Spawn daemon; write pidfile\n"
            "  stop                   Terminate via pidfile\n"
            "  status                 Show pidfile state + uptime\n"
            "  restart                stop + start\n\n"
            "NOTE: scaffold release. Full IPC listener arrives in next iteration.\n"
            "Clients currently fall back to per-invocation mode automatically.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        if (sub == "start")   return cmdStart(rest);
        if (sub == "stop")    return cmdStop(rest);
        if (sub == "status")  return cmdStatus(rest);
        if (sub == "restart") { cmdStop(rest); return cmdStart(rest); }
        std::cerr << "icmg daemon: unknown subcommand '" << sub << "'\n";
        return 1;
    }

private:
    static fs::path pidFile() {
        return fs::path(core::icmgGlobalDir()) / "daemon.pid";
    }
    static fs::path socketPath() {
#ifdef _WIN32
        // Named pipe path — for future IPC impl.
        return fs::path(R"(\\.\pipe\icmg-daemon)");
#else
        return fs::path(core::icmgGlobalDir()) / "daemon.sock";
#endif
    }

    static int64_t pidAlive(int64_t pid) {
        if (pid <= 0) return 0;
#ifdef _WIN32
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
        if (!h) return 0;
        DWORD r = WaitForSingleObject(h, 0);
        CloseHandle(h);
        return r == WAIT_TIMEOUT ? pid : 0;
#else
        return kill((pid_t)pid, 0) == 0 ? pid : 0;
#endif
    }

    int cmdStart(const std::vector<std::string>& args) {
        // Already running?
        if (fs::exists(pidFile())) {
            std::ifstream pf(pidFile());
            int64_t pid = 0; pf >> pid;
            if (pidAlive(pid)) {
                std::cout << "icmg daemon: already running (pid=" << pid << ")\n";
                return 0;
            }
            // Stale; remove.
            fs::remove(pidFile());
        }

        bool fg = hasFlag(args, "--foreground");
        fs::create_directories(pidFile().parent_path());

        if (fg) {
            // Foreground mode: write pidfile + serve loop (currently sleep stub).
#ifdef _WIN32
            int64_t pid = (int64_t)GetCurrentProcessId();
#else
            int64_t pid = (int64_t)getpid();
#endif
            std::ofstream pf(pidFile());
            pf << pid << "\n";
            pf.close();
            std::cout << "icmg daemon: started in foreground (pid=" << pid << ")\n"
                      << "  NOTE: scaffold — no IPC listener yet; sleeping.\n"
                      << "  Press Ctrl+C to stop.\n";
            // Stub loop until killed.
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(60));
            }
            return 0;
        }

        // Background: not implementing detached spawn in scaffold.
        std::cerr << "icmg daemon start: background mode requires future iteration.\n"
                  << "  Use --foreground for now: icmg daemon start --foreground\n"
                  << "  Or skip daemon entirely; clients work per-invocation.\n";
        return 1;
    }

    int cmdStop(const std::vector<std::string>&) {
        if (!fs::exists(pidFile())) {
            std::cout << "icmg daemon: not running.\n";
            return 0;
        }
        std::ifstream pf(pidFile());
        int64_t pid = 0; pf >> pid;
        pf.close();
        if (!pidAlive(pid)) {
            std::cout << "icmg daemon: stale pidfile; cleaning.\n";
            fs::remove(pidFile());
            return 0;
        }
#ifdef _WIN32
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
        if (h) { TerminateProcess(h, 0); CloseHandle(h); }
#else
        kill((pid_t)pid, SIGTERM);
#endif
        fs::remove(pidFile());
        std::cout << "icmg daemon: stopped (pid=" << pid << ").\n";
        return 0;
    }

    int cmdStatus(const std::vector<std::string>&) {
        std::cout << "icmg daemon status\n";
        if (!fs::exists(pidFile())) {
            std::cout << "  state: not running\n"
                      << "  start: icmg daemon start --foreground\n";
            return 0;
        }
        std::ifstream pf(pidFile());
        int64_t pid = 0; pf >> pid;
        if (!pidAlive(pid)) {
            std::cout << "  state: stale pidfile (pid=" << pid << " dead)\n"
                      << "  fix:   rm " << pidFile().string() << "\n";
            return 1;
        }
        std::cout << "  state: running (pid=" << pid << ")\n"
                  << "  pipe:  " << socketPath().string() << " (IPC not yet wired)\n"
                  << "  NOTE: scaffold — clients still use per-invocation mode.\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("daemon", DaemonCommand);

} // namespace icmg::cli
