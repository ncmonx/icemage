// v1.6.8: `icmg cleanup` — find + terminate orphan icmg.exe processes.
//
// Problem: under heavy hook load (cron tick + concurrent PostToolUse +
// daemon), icmg.exe instances can pile up if any one hangs (slow DB open,
// stuck wscript subprocess, etc). Each holds a SQLite WAL lock on data.db,
// causing subsequent `icmg update --apply` to fail with "unable to open
// database file".
//
// `icmg cleanup orphans` enumerates all running icmg.exe PIDs, excludes the
// service-pidfile PID (the legitimate long-runner), and reports the rest.
// With `--kill --confirm`, terminates them. Safe to run any time; the
// running icmg.exe (this process) is also excluded.
//
// Subcommands:
//   orphans                  List orphan PIDs (age, command line)
//   kill-orphans --confirm   Kill listed orphans
//   all                      List all icmg.exe instances (service + orphans)

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/path_utils.hpp"
#include "../../core/service_loop.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <algorithm>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  include <tlhelp32.h>
#  include <process.h>
#else
#  include <unistd.h>
#  include <signal.h>
#endif

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

#ifdef _WIN32
struct ProcInfo {
    DWORD pid;
    int age_min;
};

// Return PIDs of all running icmg.exe processes (excluding `self_pid`).
std::vector<ProcInfo> enumIcmgProcesses(DWORD self_pid) {
    std::vector<ProcInfo> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32 pe{}; pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            // szExeFile is char[260] on ANSI, MAX_PATH chars; case-insensitive cmp.
            std::string name = pe.szExeFile;
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (name != "icmg.exe") continue;
            if (pe.th32ProcessID == self_pid) continue;
            // Get start time → compute age (best-effort).
            int age_min = -1;
            HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                     pe.th32ProcessID);
            if (ph) {
                FILETIME create_ft, exit_ft, kernel_ft, user_ft;
                if (GetProcessTimes(ph, &create_ft, &exit_ft, &kernel_ft, &user_ft)) {
                    ULARGE_INTEGER create_ui;
                    create_ui.LowPart  = create_ft.dwLowDateTime;
                    create_ui.HighPart = create_ft.dwHighDateTime;
                    // FILETIME 100ns since 1601; convert delta to minutes.
                    FILETIME now_ft;
                    GetSystemTimeAsFileTime(&now_ft);
                    ULARGE_INTEGER now_ui;
                    now_ui.LowPart  = now_ft.dwLowDateTime;
                    now_ui.HighPart = now_ft.dwHighDateTime;
                    if (now_ui.QuadPart > create_ui.QuadPart) {
                        uint64_t delta_100ns = now_ui.QuadPart - create_ui.QuadPart;
                        age_min = (int)(delta_100ns / 10000000ull / 60ull);
                    }
                }
                CloseHandle(ph);
            }
            out.push_back({pe.th32ProcessID, age_min});
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

bool terminatePid(DWORD pid) {
    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!ph) return false;
    BOOL ok = TerminateProcess(ph, 1);
    CloseHandle(ph);
    return ok != FALSE;
}

DWORD readServicePid() {
    fs::path pidf = core::servicePidPath();
    if (!fs::exists(pidf)) return 0;
    std::ifstream f(pidf);
    long long p = 0;
    f >> p;
    return p > 0 ? (DWORD)p : 0;
}

DWORD readDaemonPid() {
    // rule-daemon pidfile.
    fs::path pidf = fs::path(core::icmgGlobalDir()) / "rule-daemon.pid";
    if (!fs::exists(pidf)) return 0;
    std::ifstream f(pidf);
    long long p = 0;
    f >> p;
    return p > 0 ? (DWORD)p : 0;
}
#endif

}  // namespace

class CleanupCommand : public BaseCommand {
public:
    std::string name()        const override { return "cleanup"; }
    std::string description() const override {
        return "Find + terminate orphan icmg.exe processes (DB-lock contention fix)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg cleanup <action>\n\n"
            "Actions:\n"
            "  orphans                List orphan icmg.exe PIDs (excludes service + daemon + self)\n"
            "  kill-orphans --confirm Terminate listed orphans\n"
            "  all                    List all icmg.exe instances (including service)\n";
    }

    int run(const std::vector<std::string>& args) override {
#ifndef _WIN32
        (void)args;
        std::cout << "icmg cleanup: POSIX no-op (no orphan-icmg scenario observed).\n";
        return 0;
#else
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        DWORD self_pid    = (DWORD)_getpid();
        DWORD service_pid = readServicePid();
        DWORD daemon_pid  = readDaemonPid();
        auto all = enumIcmgProcesses(self_pid);

        if (sub == "all") {
            std::cout << std::left
                      << std::setw(8)  << "PID"
                      << std::setw(8)  << "age"
                      << "role\n";
            for (auto& p : all) {
                std::string role = "orphan";
                if (p.pid == service_pid) role = "service";
                else if (p.pid == daemon_pid) role = "daemon";
                std::cout << std::left
                          << std::setw(8) << p.pid
                          << std::setw(8) << (std::to_string(p.age_min) + "m")
                          << role << "\n";
            }
            std::cout << "Total: " << all.size() << " (self=" << self_pid
                      << " excluded; service=" << service_pid
                      << " daemon=" << daemon_pid << ")\n";
            return 0;
        }

        std::vector<DWORD> orphans;
        for (auto& p : all) {
            if (p.pid == service_pid) continue;
            if (p.pid == daemon_pid)  continue;
            orphans.push_back(p.pid);
        }

        if (sub == "orphans") {
            if (orphans.empty()) {
                std::cout << "icmg cleanup: no orphan icmg.exe processes.\n";
                return 0;
            }
            std::cout << "Orphan icmg.exe PIDs (" << orphans.size() << "):\n";
            for (auto& p : all) {
                if (p.pid == service_pid || p.pid == daemon_pid) continue;
                std::cout << "  PID=" << p.pid
                          << " age=" << p.age_min << "m\n";
            }
            std::cout << "\nKill all: icmg cleanup kill-orphans --confirm\n";
            return 0;
        }

        if (sub == "kill-orphans") {
            if (!hasFlag(args, "--confirm")) {
                std::cerr << "icmg cleanup kill-orphans: pass --confirm to proceed.\n";
                std::cerr << "Run `icmg cleanup orphans` first to review.\n";
                return 1;
            }
            int killed = 0, failed = 0;
            for (DWORD pid : orphans) {
                if (terminatePid(pid)) ++killed;
                else ++failed;
            }
            std::cout << "icmg cleanup: killed " << killed
                      << " orphan(s), failed " << failed << "\n";
            return failed == 0 ? 0 : 1;
        }

        std::cerr << "icmg cleanup: unknown action '" << sub << "'\n";
        usage();
        return 1;
#endif
    }
};

ICMG_REGISTER_COMMAND("cleanup", CleanupCommand);

}  // namespace icmg::cli
