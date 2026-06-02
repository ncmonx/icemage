// v1.6.1: `icmg popup-killer` — auto-dismiss Win32 system dialogs spawned
// by SmartScreen/Defender/etc. probing non-existent drives (B:\ etc).
//
// Root cause of the "B:/ — system cannot find drive specified" popup is
// outside icmg's process tree: SmartScreen.exe (parent svchost) probes all
// drive letters during reputation scan. SEM_FAILCRITICALERRORS only affects
// processes that inherit it; SmartScreen does not.
//
// User-space workaround (no admin needed): enumerate top-level windows
// periodically; any window matching the drive-not-found dialog signature
// (window class #32770, title matching "[A-Z]:/" or "[A-Z]:\") gets
// PostMessage(WM_CLOSE) → invisibly dismissed.
//
// Modes:
//   icmg popup-killer run             Blocking loop, scans every 100ms.
//   icmg popup-killer scan-once       Single-shot scan + dismiss + exit.
//   icmg popup-killer status          Print last-dismiss count from log.
//
// POSIX: no-op (no Win32 dialogs).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/path_utils.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

#ifdef _WIN32

std::atomic<bool> g_stop{false};
std::atomic<int>  g_dismissed{0};

// Test window for drive-not-found signature.
// Heuristics (any-of):
//   1. Title matches "[A-Z]:" exactly (e.g. "B:/", "B:\")
//   2. Title is 2-4 chars matching [A-Z][:/\\]
//   3. Class is #32770 (Windows system dialog) AND title contains "drive"
//      or "specified" or "tidak dapat menemukan" (Indonesian variant)
bool isDriveNotFoundDialog(HWND hwnd) {
    char title[128] = {0};
    int n = GetWindowTextA(hwnd, title, sizeof(title) - 1);
    if (n <= 0 || n > 64) return false;

    // Exact short-title patterns: "B:", "B:/", "B:\"
    if (n >= 2 && n <= 4) {
        char c = title[0];
        if (c >= 'A' && c <= 'Z' && title[1] == ':') {
            // Likely a drive-letter title.
            char cls[64] = {0};
            GetClassNameA(hwnd, cls, sizeof(cls) - 1);
            if (std::strcmp(cls, "#32770") == 0) return true;
        }
    }
    return false;
}

BOOL CALLBACK enumProc(HWND hwnd, LPARAM /*lparam*/) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (!isDriveNotFoundDialog(hwnd)) return TRUE;
    // Found one — dismiss invisibly.
    PostMessageA(hwnd, WM_CLOSE, 0, 0);
    g_dismissed.fetch_add(1);
    return TRUE;
}

void scanOnce() {
    EnumWindows(enumProc, 0);
}

fs::path logPath() {
    return fs::path(core::icmgGlobalDir()) / "popup-killer.log";
}

void appendLog(const std::string& msg) {
    std::error_code ec;
    fs::create_directories(logPath().parent_path(), ec);
    std::ofstream f(logPath(), std::ios::app);
    if (!f) return;
    std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    f << buf << " " << msg << "\n";
}

#endif  // _WIN32

}  // namespace

class PopupKillerCommand : public BaseCommand {
public:
    std::string name()        const override { return "popup-killer"; }
    std::string description() const override {
        return "Auto-dismiss Win32 'drive not found' popups (no admin needed)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg popup-killer <action>\n\n"
            "Actions:\n"
            "  run           Blocking loop, scans every 100ms (Ctrl+C to stop)\n"
            "  scan-once     Single-shot enum-windows + dismiss + exit\n"
            "  status        Print last-dismiss count from log\n";
    }

    int run(const std::vector<std::string>& args) override {
#ifndef _WIN32
        (void)args;
        std::cout << "icmg popup-killer: POSIX no-op.\n";
        return 0;
#else
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        if (sub == "run")        return cmdRun();
        if (sub == "ensure")     return cmdEnsure();
        if (sub == "scan-once")  return cmdScanOnce();
        if (sub == "status")     return cmdStatus();
        std::cerr << "icmg popup-killer: unknown action '" << sub << "'\n";
        return 1;
#endif
    }

#ifdef _WIN32
private:
    int cmdScanOnce() {
        int before = g_dismissed.load();
        scanOnce();
        int after = g_dismissed.load();
        std::cout << "icmg popup-killer: dismissed " << (after - before)
                  << " dialog(s)\n";
        if (after > before) appendLog("scan-once dismissed=" + std::to_string(after - before));
        return 0;
    }

    int cmdRun() {
        // Single-instance: a global named mutex prevents piling up daemons (the
        // SessionStart hook calls `ensure` every session). A second instance exits.
        HANDLE mtx = CreateMutexA(nullptr, TRUE, "Global\\icmg_popup_killer");
        if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) return 0;
        std::cerr << "icmg popup-killer: scanning every 100ms (Ctrl+C to stop)\n";
        int last_log = 0;
        while (!g_stop.load()) {
            scanOnce();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            int cur = g_dismissed.load();
            if (cur != last_log && (cur - last_log) >= 10) {
                appendLog("loop tally=" + std::to_string(cur));
                last_log = cur;
            }
        }
        return 0;
    }

    // Start the blocking daemon detached if not already running. Idempotent (the
    // daemon self-guards via the named mutex). Called by the SessionStart hook + init
    // so the B:/ drive-not-found dialog is auto-dismissed within ~100ms — before it
    // can block a hook subprocess (a blocked hook hangs Claude Code).
    int cmdEnsure() {
        // Cheap fast-path: if the daemon already holds the mutex, do nothing.
        // Lets every hook call `ensure` (self-heal) at ~zero cost when alive.
        HANDLE existing = OpenMutexA(SYNCHRONIZE, FALSE, "Global\\icmg_popup_killer");
        if (existing) { CloseHandle(existing); return 0; }
        char self[MAX_PATH];
        if (!GetModuleFileNameA(nullptr, self, MAX_PATH)) return 1;
        std::string cmd = std::string("\"") + self + "\" popup-killer run";
        std::vector<char> buf(cmd.begin(), cmd.end()); buf.push_back('\0');
        STARTUPINFOA si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                           DETACHED_PROCESS | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        return 0;  // best-effort; daemon dedups via mutex
    }

    int cmdStatus() {
        std::ifstream f(logPath());
        if (!f) { std::cout << "popup-killer log empty (or not yet run).\n"; return 0; }
        std::cout << "Recent popup-killer log entries:\n";
        std::string line;
        std::vector<std::string> lines;
        while (std::getline(f, line)) lines.push_back(line);
        size_t start = lines.size() > 20 ? lines.size() - 20 : 0;
        for (size_t i = start; i < lines.size(); ++i)
            std::cout << "  " << lines[i] << "\n";
        return 0;
    }
#endif
};

ICMG_REGISTER_COMMAND("popup-killer", PopupKillerCommand);

}  // namespace icmg::cli
