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
#include "../drive_dialog_match.hpp"
#include <cctype>

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

// DIAGNOSTIC (2026-06-12): log the matched dialog's owner process + full text
// before dismissing. The body usually names the exact drive/path being probed
// (e.g. "...drive B:\..."), which reveals the elusive source of the popup.
void logDialogDetail(HWND hwnd) {
    char title[128] = {0}; GetWindowTextA(hwnd, title, sizeof(title) - 1);
    std::string body;
    EnumChildWindows(hwnd, [](HWND c, LPARAM lp) -> BOOL {
        char b[256] = {0};
        if (GetWindowTextA(c, b, sizeof(b) - 1) > 0) {
            auto* s = reinterpret_cast<std::string*>(lp); *s += b; *s += " | ";
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&body));
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    char pname[MAX_PATH] = "?";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) { DWORD n = MAX_PATH; QueryFullProcessImageNameA(h, 0, pname, &n); CloseHandle(h); }
    std::error_code ec; fs::create_directories(core::icmgGlobalDir(), ec);
    std::ofstream f(fs::path(core::icmgGlobalDir()) / "popup-killer.log", std::ios::app);
    if (f) f << "DIALOG pid=" << pid << " proc=" << pname
             << " title=[" << title << "] body=[" << body << "]\n";
}

// Test window for drive-not-found signature.
// Heuristics (any-of):
//   1. Title matches "[A-Z]:" exactly (e.g. "B:/", "B:\")
//   2. Title is 2-4 chars matching [A-Z][:/\\]
//   3. Class is #32770 (Windows system dialog) AND title contains "drive"
//      or "specified" or "tidak dapat menemukan" (Indonesian variant)
bool isDriveNotFoundDialog(HWND hwnd) {
    char cls[64] = {0};
    GetClassNameA(hwnd, cls, sizeof(cls) - 1);
    char title[128] = {0};
    GetWindowTextA(hwnd, title, sizeof(title) - 1);
    std::string body;
    EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
        char buf[256] = {0};
        if (GetWindowTextA(child, buf, sizeof(buf) - 1) > 0) {
            auto* b = reinterpret_cast<std::string*>(lp); *b += buf; *b += ' ';
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&body));
    for (char& ch : body) ch = (char)std::tolower((unsigned char)ch);
    bool bodyHasDrive = body.find("drive") != std::string::npos
                     || body.find("specified") != std::string::npos
                     || body.find("tidak dapat menemukan") != std::string::npos;
    return driveDialogMatch(std::string(title), std::string(cls), bodyHasDrive);
}

BOOL CALLBACK enumProc(HWND hwnd, LPARAM /*lparam*/) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (!isDriveNotFoundDialog(hwnd)) return TRUE;
    // Found one — log its source then dismiss invisibly.
    logDialogDetail(hwnd);
    PostMessageA(hwnd, WM_CLOSE, 0, 0);
    g_dismissed.fetch_add(1);
    return TRUE;
}

void scanOnce() {
    EnumWindows(enumProc, 0);
}

// Event-driven instant dismiss: fires the moment a dialog is shown, so the
// drive-not-found popup is closed before it paints a visible frame (the 100ms
// poll alone left a perceptible flash -- a health concern for some users).
// Cheap pre-filter on class #32770 (system dialog) keeps this fast even though
// the hook sees every window-show system-wide.
void CALLBACK winEventProc(HWINEVENTHOOK, DWORD /*event*/, HWND hwnd,
                           LONG idObject, LONG /*idChild*/,
                           DWORD /*thread*/, DWORD /*time*/) {
    if (!hwnd || idObject != OBJID_WINDOW) return;
    char cls[16] = {0};
    if (GetClassNameA(hwnd, cls, sizeof(cls) - 1) <= 0) return;
    if (std::strcmp(cls, "#32770") != 0) return;   // system dialogs only
    if (isDriveNotFoundDialog(hwnd)) {
        logDialogDetail(hwnd);
        PostMessageA(hwnd, WM_CLOSE, 0, 0);
        g_dismissed.fetch_add(1);
    }
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
        // Local\ namespace: Global\ needs SeCreateGlobalPrivilege -> CreateMutexA
        // fails ACCESS_DENIED on a non-elevated session -> guard fell open (#31901).
        HANDLE mtx = CreateMutexA(nullptr, TRUE, "Local\\icmg_popup_killer");
        if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) return 0;
        std::cerr << "icmg popup-killer: event-driven dismiss + 100ms sweep (Ctrl+C)\n";
        // Primary: dismiss the instant a dialog is shown (kills the flash).
        HWINEVENTHOOK hook = SetWinEventHook(
            EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
            nullptr, winEventProc, 0, 0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        // Fallback floor: keep the prior 100ms sweep for anything the event
        // misses (dialogs already open before we started, missed events).
        SetTimer(nullptr, 1, 100, nullptr);
        int last_log = 0;
        MSG msg;
        while (!g_stop.load() && GetMessageA(&msg, nullptr, 0, 0)) {
            if (msg.message == WM_TIMER) {
                scanOnce();
                int cur = g_dismissed.load();
                if (cur != last_log && (cur - last_log) >= 10) {
                    appendLog("loop tally=" + std::to_string(cur));
                    last_log = cur;
                }
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (hook) UnhookWinEvent(hook);
        return 0;
    }

    // Start the blocking daemon detached if not already running. Idempotent (the
    // daemon self-guards via the named mutex). Called by the SessionStart hook + init
    // so the B:/ drive-not-found dialog is auto-dismissed within ~100ms — before it
    // can block a hook subprocess (a blocked hook hangs Claude Code).
    int cmdEnsure() {
        // Cheap fast-path: if the daemon already holds the mutex, do nothing.
        // Lets every hook call `ensure` (self-heal) at ~zero cost when alive.
        HANDLE existing = OpenMutexA(SYNCHRONIZE, FALSE, "Local\\icmg_popup_killer");
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
