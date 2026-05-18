// v1.1.1 — Resident-service auto-activation helper implementation.
#include "service_install.hpp"
#include "path_utils.hpp"
#include "exec_utils.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::core {

namespace {

constexpr const char* LEGACY_PREFIXES[] = {
    "icmg-backup-",
    "icmg-maintain-",
    "icmg-mirror-",
    "icmg-sentinel-",
    "icmg-shadow-upgrade",
};

bool startsWithAny(const std::string& s) {
    for (const char* p : LEGACY_PREFIXES) {
        std::string prefix(p);
        if (s.size() >= prefix.size() &&
            s.compare(0, prefix.size(), prefix) == 0) {
            // Don't match the new "icmg-service" itself.
            if (s == "icmg-service") return false;
            return true;
        }
    }
    return false;
}

} // namespace

bool installResidentService(std::string* err_out) {
    auto setErr = [&](const std::string& m) { if (err_out) *err_out = m; };
    if (err_out) err_out->clear();

    if (std::getenv("ICMG_SKIP_SERVICE")) {
        return true;  // explicit opt-out
    }

#ifdef _WIN32
    // 1) Write VBS launcher (idempotent: overwrite is safe).
    fs::path gdir = fs::path(icmgGlobalDir());
    std::error_code ec;
    fs::create_directories(gdir, ec);
    if (ec) { setErr("mkdir failed: " + ec.message()); return false; }

    fs::path vbs = gdir / "service-launcher.vbs";
    {
        std::ofstream f(vbs, std::ios::binary);
        if (!f) { setErr("cannot write VBS launcher"); return false; }
        f << "CreateObject(\"Wscript.Shell\").Run \"icmg service run\", 0, False\r\n";
    }

    // 2) Register logon-trigger task (overwrite with /F). v1.6.1: use cmd.exe
    // direct prefix instead of `MSYS_NO_PATHCONV=1` — when invoked from
    // PowerShell, the bash VAR=value form was emitting truncated/garbled
    // errors (PS interpreted leading token as a cmdlet).
    std::string cmd =
        "MSYS_NO_PATHCONV=1 schtasks /Create /SC ONLOGON /TN \"icmg-service\""
        " /TR \"wscript.exe //B //Nologo \\\"" + vbs.string() + "\\\"\""
        " /F";
    auto r = safeExecShell(cmd, true, 15000);
    if (r.exit_code != 0) {
        // v1.6.1: fall back to user Startup folder shortcut — no admin needed.
        // Service still auto-starts at next user logon (via Explorer shell
        // startup-folder enumeration). Less robust than schtask (no resume on
        // logout/login mid-session) but works for shared servers + standard
        // user accounts.
        const char* appdata = std::getenv("APPDATA");
        if (appdata && *appdata) {
            fs::path startup = fs::path(appdata) / "Microsoft" / "Windows"
                              / "Start Menu" / "Programs" / "Startup";
            std::error_code ec2;
            fs::create_directories(startup, ec2);
            // Build a VBScript that writes the .lnk (Win Shell COM via Wscript.Shell).
            fs::path mklnk = gdir / "service-mklnk.vbs";
            fs::path lnk   = startup / "icmg-service.lnk";
            {
                std::ofstream f(mklnk, std::ios::binary);
                if (f) {
                    f << "Set sh = CreateObject(\"Wscript.Shell\")\r\n"
                      << "Set lk = sh.CreateShortcut(\"" << lnk.string() << "\")\r\n"
                      << "lk.TargetPath = \"wscript.exe\"\r\n"
                      << "lk.Arguments  = \"//B //Nologo \"\"" << vbs.string() << "\"\"\"\r\n"
                      << "lk.WindowStyle = 7\r\n"
                      << "lk.Save\r\n";
                }
            }
            std::string lcmd = "cmd.exe /c wscript.exe //B //Nologo \""
                             + mklnk.string() + "\"";
            auto rlnk = safeExecShell(lcmd, true, 10000);
            std::error_code ec3;
            if (rlnk.exit_code == 0 && fs::exists(lnk, ec3)) {
                // Fallback succeeded. Best-effort fire the service NOW so
                // popup-killer + cron iterator start immediately without wait
                // for next logon.
                std::string boot = "cmd.exe /c wscript.exe //B //Nologo \""
                                 + vbs.string() + "\"";
                (void)safeExecShell(boot, true, 5000);
                return true;
            }
        }
        std::string err = r.err.empty() ? r.out : r.err;
        bool denied = err.find("denied") != std::string::npos
                   || err.find("Access") != std::string::npos
                   || err.find("akses") != std::string::npos
                   || r.exit_code == 5
                   || r.exit_code == 1314;
        if (denied) {
            setErr("elevation denied + Startup-folder fallback failed. "
                   "Manual: copy " + vbs.string() + " shortcut to "
                   "%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\");
        } else {
            setErr("schtasks failed (exit=" + std::to_string(r.exit_code)
                   + "): " + err);
        }
        return false;
    }
    return true;
#else
    (void)err_out;
    return true;  // POSIX: no-op (systemd/launchd out of scope)
#endif
}

int cleanupLegacySchtasks() {
    if (std::getenv("ICMG_SKIP_SERVICE")) return 0;

#ifdef _WIN32
    // Enumerate task names — CSV, no header, first column is task path.
    auto q = safeExecShell(
        "MSYS_NO_PATHCONV=1 schtasks /Query /FO CSV /NH", false, 15000);
    if (q.exit_code != 0) return 0;

    int removed = 0;
    std::istringstream iss(q.out);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        // First CSV column: "\name" or "\folder\name", quoted.
        // Strip leading quote.
        size_t start = (line.size() > 0 && line[0] == '"') ? 1 : 0;
        size_t end = line.find('"', start);
        if (end == std::string::npos) continue;
        std::string col = line.substr(start, end - start);
        // Strip leading backslash.
        if (!col.empty() && col[0] == '\\') col.erase(0, 1);
        // We only care about top-level tasks (no folder).
        if (col.find('\\') != std::string::npos) continue;

        if (!startsWithAny(col)) continue;

        std::string del =
            "MSYS_NO_PATHCONV=1 schtasks /Delete /TN \"" + col + "\" /F";
        auto dr = safeExecShell(del, false, 8000);
        if (dr.exit_code == 0) ++removed;
    }
    return removed;
#else
    return 0;
#endif
}

} // namespace icmg::core
