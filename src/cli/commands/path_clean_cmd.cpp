// v1.10.0: `icmg path-clean` — strip non-existent drive-letter entries from
// Windows user/system PATH env var. Eliminates `B:/` popup at root.
//
// PROBLEM: When a shell (cmd.exe / bash / PowerShell) does PATH lookup to
// resolve `icmg` (or any binary), it iterates every PATH entry. Entries
// pointing to a non-existent drive (`B:\legacy\bin`) cause Win32 to raise
// the "B:/ — system cannot find the drive specified" modal dialog BEFORE
// the target binary even starts. v1.7.0 launcher's `sanitize_path()` can't
// help here — it only runs AFTER icmg.exe is found and executed.
//
// FIX: rewrite the persisted PATH env var (registry-level) to remove dead
// drive entries. Once persisted, every new shell + GUI app picks up the
// cleaned PATH. WM_SETTINGCHANGE broadcast notifies live processes.
//
// Subcommands:
//   status            Dry-run: list bad entries from User + System PATH
//   apply             Clean User PATH (HKCU\Environment\Path); no admin needed
//   apply --system    Also clean System PATH (HKLM\…\Environment\Path); admin
//
// POSIX: no-op (no Win32 shell drive-probe issue).

#include "../base_command.hpp"
#include "../../core/registry.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace icmg::cli {

namespace {

#ifdef _WIN32

// Read REG_EXPAND_SZ or REG_SZ string from registry key/value.
// Returns empty on failure. Caller must call cleanup if needed.
bool readRegPath(HKEY root, const char* subkey, const char* value,
                 std::string& out, bool& was_expand) {
    HKEY hk;
    LONG r = RegOpenKeyExA(root, subkey, 0, KEY_READ, &hk);
    if (r != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    r = RegQueryValueExA(hk, value, nullptr, &type, nullptr, &size);
    if (r != ERROR_SUCCESS || size == 0) { RegCloseKey(hk); return false; }
    std::vector<char> buf(size + 1, 0);
    r = RegQueryValueExA(hk, value, nullptr, &type,
                         reinterpret_cast<LPBYTE>(buf.data()), &size);
    RegCloseKey(hk);
    if (r != ERROR_SUCCESS) return false;
    out.assign(buf.data());
    // Strip trailing NULs that may sneak in from REG_SZ length.
    while (!out.empty() && out.back() == '\0') out.pop_back();
    was_expand = (type == REG_EXPAND_SZ);
    return true;
}

bool writeRegPath(HKEY root, const char* subkey, const char* value,
                  const std::string& data, bool as_expand) {
    HKEY hk;
    LONG r = RegOpenKeyExA(root, subkey, 0, KEY_WRITE, &hk);
    if (r != ERROR_SUCCESS) return false;
    DWORD type = as_expand ? REG_EXPAND_SZ : REG_SZ;
    r = RegSetValueExA(hk, value, 0, type,
                       reinterpret_cast<const BYTE*>(data.c_str()),
                       (DWORD)(data.size() + 1));
    RegCloseKey(hk);
    return r == ERROR_SUCCESS;
}

// Split PATH on ';', trim empty entries.
std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : path) {
        if (c == ';') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string joinPath(const std::vector<std::string>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += ';';
        out += parts[i];
    }
    return out;
}

// Returns true if entry is a drive-letter path with non-existent drive.
// Empty / non-drive entries return false (kept).
bool isDeadDriveEntry(const std::string& entry, DWORD drives) {
    if (entry.size() < 2) return false;
    char c0 = entry[0], c1 = entry[1];
    if (c1 != ':') return false;
    char drv = c0;
    if (drv >= 'a' && drv <= 'z') drv = (char)(drv - 32);
    if (drv < 'A' || drv > 'Z') return false;
    int bit = drv - 'A';
    return !((drives >> bit) & 1);
}

struct CleanResult {
    std::vector<std::string> kept;
    std::vector<std::string> dropped;
};

CleanResult cleanPath(const std::string& path) {
    CleanResult cr;
    DWORD drives = GetLogicalDrives();
    for (auto& e : splitPath(path)) {
        if (isDeadDriveEntry(e, drives)) cr.dropped.push_back(e);
        else                              cr.kept.push_back(e);
    }
    return cr;
}

void broadcastEnvChange() {
    DWORD_PTR ignored;
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>("Environment"),
                        SMTO_ABORTIFHUNG, 5000, &ignored);
}

void reportSection(const std::string& label, const CleanResult& cr) {
    std::cout << "[" << label << "]\n";
    if (cr.dropped.empty()) {
        std::cout << "  No dead-drive entries. " << cr.kept.size()
                  << " entries kept.\n";
    } else {
        std::cout << "  " << cr.dropped.size() << " dead-drive entr"
                  << (cr.dropped.size() == 1 ? "y" : "ies") << " found:\n";
        for (auto& e : cr.dropped) std::cout << "    - " << e << "\n";
        std::cout << "  " << cr.kept.size() << " entries kept.\n";
    }
}

#endif  // _WIN32

}  // namespace

class PathCleanCommand : public BaseCommand {
public:
    std::string name()        const override { return "path-clean"; }
    std::string description() const override {
        return "Strip dead-drive entries from Windows PATH (kills B:/ popup)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg path-clean <action> [--system]\n\n"
            "Actions:\n"
            "  status            Dry-run: list dead-drive entries in User+System PATH\n"
            "  apply             Clean User PATH (HKCU; no admin needed)\n"
            "  apply --system    Also clean System PATH (HKLM; needs admin)\n\n"
            "Root cause of B:/ popup: shell PATH lookup probes every PATH\n"
            "entry. Entries on non-existent drives raise modal dialogs BEFORE\n"
            "icmg.exe runs (so launcher sanitize_path can't help). This cmd\n"
            "rewrites the persisted PATH env var so future shells stay clean.\n";
    }

    int run(const std::vector<std::string>& args) override {
#ifndef _WIN32
        (void)args;
        std::cout << "icmg path-clean: POSIX no-op (no Win32 drive-probe issue).\n";
        return 0;
#else
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        bool do_system = hasFlag(args, "--system");

        // Read User PATH (HKCU\Environment\Path).
        std::string user_path, sys_path;
        bool user_expand = false, sys_expand = false;
        bool user_ok = readRegPath(HKEY_CURRENT_USER,
                                    "Environment", "Path",
                                    user_path, user_expand);
        bool sys_ok  = readRegPath(HKEY_LOCAL_MACHINE,
                                    "System\\CurrentControlSet\\Control\\Session Manager\\Environment",
                                    "Path", sys_path, sys_expand);

        CleanResult user_cr = user_ok ? cleanPath(user_path) : CleanResult{};
        CleanResult sys_cr  = sys_ok  ? cleanPath(sys_path)  : CleanResult{};

        if (sub == "status") {
            if (user_ok) reportSection("User PATH (HKCU)", user_cr);
            else         std::cout << "[User PATH (HKCU)] (read failed)\n";
            if (sys_ok)  reportSection("System PATH (HKLM)", sys_cr);
            else         std::cout << "[System PATH (HKLM)] (read failed; need admin)\n";
            int total_bad = (int)user_cr.dropped.size() + (int)sys_cr.dropped.size();
            std::cout << "\nTotal dead-drive entries: " << total_bad << "\n";
            if (total_bad > 0) {
                std::cout << "\nFix:\n"
                          << "  icmg path-clean apply              (user PATH only)\n"
                          << "  icmg path-clean apply --system     (also system PATH, run as admin)\n";
            }
            return 0;
        }

        if (sub == "apply") {
            int changed = 0;
            if (user_ok && !user_cr.dropped.empty()) {
                std::string clean = joinPath(user_cr.kept);
                if (writeRegPath(HKEY_CURRENT_USER, "Environment", "Path",
                                  clean, user_expand)) {
                    std::cout << "User PATH cleaned: " << user_cr.dropped.size()
                              << " entr" << (user_cr.dropped.size() == 1 ? "y" : "ies")
                              << " removed.\n";
                    for (auto& e : user_cr.dropped) std::cout << "  - " << e << "\n";
                    ++changed;
                } else {
                    std::cerr << "icmg path-clean: failed to write HKCU\\Environment\\Path\n";
                    return 1;
                }
            } else if (user_ok) {
                std::cout << "User PATH already clean.\n";
            }

            if (do_system) {
                if (sys_ok && !sys_cr.dropped.empty()) {
                    std::string clean = joinPath(sys_cr.kept);
                    if (writeRegPath(HKEY_LOCAL_MACHINE,
                                      "System\\CurrentControlSet\\Control\\Session Manager\\Environment",
                                      "Path", clean, sys_expand)) {
                        std::cout << "System PATH cleaned: " << sys_cr.dropped.size()
                                  << " entr" << (sys_cr.dropped.size() == 1 ? "y" : "ies")
                                  << " removed.\n";
                        for (auto& e : sys_cr.dropped) std::cout << "  - " << e << "\n";
                        ++changed;
                    } else {
                        std::cerr << "icmg path-clean: failed to write HKLM PATH "
                                  << "(need admin? error=" << GetLastError() << ")\n";
                        return 1;
                    }
                } else if (sys_ok) {
                    std::cout << "System PATH already clean.\n";
                } else {
                    std::cerr << "icmg path-clean: cannot read HKLM PATH (need admin)\n";
                    return 1;
                }
            }

            if (changed > 0) {
                broadcastEnvChange();
                std::cout << "\nBroadcast WM_SETTINGCHANGE → new shells will pick up cleaned PATH.\n"
                          << "Restart Claude Code + any open terminals for popup to stop.\n";
            }
            return 0;
        }

        std::cerr << "icmg path-clean: unknown action '" << sub << "'\n";
        usage();
        return 1;
#endif
    }
};

ICMG_REGISTER_COMMAND("path-clean", PathCleanCommand);

}  // namespace icmg::cli
