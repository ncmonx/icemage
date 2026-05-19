// v1.13.0: `icmg uninstall` — clean removal of all icmg state.
//
// Removes: schtask, Startup-folder shortcut, daemon pidfiles, ~/.icmg/,
// ~/bin/icmg.exe + icmg-core.exe + DLLs (when ICMG_BIN_DIR detected).
// Refuses without --confirm. POSIX: removes systemd unit / launchd plist.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/path_utils.hpp"
#include "../../core/exec_utils.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  include <tlhelp32.h>
#endif

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

void killAllIcmgProcs() {
#ifdef _WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32 pe{}; pe.dwSize = sizeof(pe);
    DWORD self = GetCurrentProcessId();
    if (Process32First(snap, &pe)) {
        do {
            std::string name = pe.szExeFile;
            for (auto& c : name) c = (char)tolower((unsigned char)c);
            if (name != "icmg.exe" && name != "icmg-core.exe") continue;
            if (pe.th32ProcessID == self) continue;
            HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (ph) { TerminateProcess(ph, 0); CloseHandle(ph); }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
#endif
}

} // namespace

class UninstallCommand : public BaseCommand {
public:
    std::string name()        const override { return "uninstall"; }
    std::string description() const override {
        return "Remove icmg state + schtasks + binaries (asks --confirm)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg uninstall [--dry-run] [--confirm] [--keep-state]\n\n"
            "Removes per-user icmg state. Default = dry-run (lists targets).\n\n"
            "Flags:\n"
            "  --dry-run     (default) list what would be removed\n"
            "  --confirm     actually remove\n"
            "  --keep-state  preserve ~/.icmg/ (DB + memory + logs)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool confirm = hasFlag(args, "--confirm");
        bool dry     = !confirm;
        bool keep    = hasFlag(args, "--keep-state");

        std::vector<std::string> targets;
        std::error_code ec;
        fs::path home;
#ifdef _WIN32
        const char* up = std::getenv("USERPROFILE");
        if (up) home = up;
#else
        const char* hh = std::getenv("HOME");
        if (hh) home = hh;
#endif

        // 1. Schtask icmg-service.
#ifdef _WIN32
        targets.push_back("Windows schtask: icmg-service");
        // 2. Startup folder shortcut.
        const char* appdata = std::getenv("APPDATA");
        if (appdata) {
            fs::path lnk = fs::path(appdata) / "Microsoft" / "Windows"
                         / "Start Menu" / "Programs" / "Startup" / "icmg-service.lnk";
            if (fs::exists(lnk, ec)) targets.push_back(lnk.string());
        }
#else
        // POSIX: systemd unit + launchd plist
        if (!home.empty()) {
            fs::path sd = home / ".config" / "systemd" / "user" / "icmg.service";
            if (fs::exists(sd, ec)) targets.push_back(sd.string());
            fs::path la = home / "Library" / "LaunchAgents" / "com.icmg.service.plist";
            if (fs::exists(la, ec)) targets.push_back(la.string());
        }
#endif

        // 3. State dir.
        if (!keep && !home.empty()) {
            fs::path state = home / ".icmg";
            if (fs::exists(state, ec)) targets.push_back(state.string());
        }

        // 4. Binaries (best-guess).
        if (!home.empty()) {
            fs::path bindir = home / "bin";
#ifdef _WIN32
            for (auto* n : {"icmg.exe", "icmg-core.exe", "libwinpthread-1.dll",
                            "libtree-sitter-0.26.dll", "libzstd.dll",
                            "wasmtime.dll", "onnxruntime.dll",
                            "onnxruntime_providers_shared.dll"}) {
                fs::path f = bindir / n;
                if (fs::exists(f, ec)) targets.push_back(f.string());
            }
#else
            for (auto* n : {"icmg", "icmg-core"}) {
                fs::path f = bindir / n;
                if (fs::exists(f, ec)) targets.push_back(f.string());
            }
#endif
        }

        std::cout << "icmg uninstall " << (dry ? "[DRY-RUN]" : "[EXECUTE]") << "\n";
        std::cout << "Targets (" << targets.size() << "):\n";
        for (auto& t : targets) std::cout << "  - " << t << "\n";

        if (dry) {
            std::cout << "\nRun with --confirm to actually remove.\n";
            return 0;
        }

        // 1. Kill running icmg procs first.
        killAllIcmgProcs();

#ifdef _WIN32
        // Remove schtask.
        (void)core::safeExecShell(
            "MSYS_NO_PATHCONV=1 schtasks /Delete /TN \"icmg-service\" /F",
            true, 8000);
#else
        if (!home.empty()) {
            std::string disable = "systemctl --user disable --now icmg.service 2>/dev/null";
            (void)core::safeExecShell(disable, true, 5000);
            std::string unload = "launchctl unload " + home.string()
                + "/Library/LaunchAgents/com.icmg.service.plist 2>/dev/null";
            (void)core::safeExecShell(unload, true, 5000);
        }
#endif

        // Remove paths.
        int removed = 0, failed = 0;
        for (auto& t : targets) {
            if (t.rfind("Windows schtask", 0) == 0) continue;  // handled
            std::error_code rec;
            if (fs::is_directory(t, rec)) {
                fs::remove_all(t, rec);
            } else {
                fs::remove(t, rec);
            }
            if (rec) { std::cerr << "  ! " << t << ": " << rec.message() << "\n"; ++failed; }
            else     { ++removed; }
        }
        std::cout << "\nuninstall: removed " << removed
                  << " path(s), " << failed << " failed.\n";
        return failed == 0 ? 0 : 1;
    }
};

ICMG_REGISTER_COMMAND("uninstall", UninstallCommand);

}  // namespace icmg::cli
