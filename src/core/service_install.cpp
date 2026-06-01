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

#ifndef _WIN32
#  include <unistd.h>
#  ifdef __APPLE__
#    include <mach-o/dyld.h>
#  endif
#endif

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
        // v1.27.2: revert to `icmg service run` — v1.19.1 collapsed dual-binary
        // (icmg-core dropped). VBS targeted non-existent icmg-core.exe → silent
        // fail → service never started → popup-killer thread never ran →
        // recurring B:/ popup persisted. Single binary `icmg.exe` IS the long-
        // running proc; no wrapper needed.
        f << "CreateObject(\"Wscript.Shell\").Run \"icmg service run\", 0, False\r\n";
    }

    // 2) Register logon-trigger task (overwrite with /F). v1.6.1: use cmd.exe
    // direct prefix instead of `MSYS_NO_PATHCONV=1` — when invoked from
    // PowerShell, the bash VAR=value form was emitting truncated/garbled
    // errors (PS interpreted leading token as a cmdlet).
    // v1.28.1: ICMG_SERVICE_NO_SCHTASKS=1 env opt-out skips the schtasks
    // attempt entirely and goes straight to user Startup folder fallback.
    // For shared-server admins or standard users who already know schtasks
    // /Create will fail without elevation. Avoids the "Access is denied"
    // stderr noise on every `icmg init --force`.
    const char* no_schtasks = std::getenv("ICMG_SERVICE_NO_SCHTASKS");
    bool skip_schtasks = no_schtasks && *no_schtasks && std::string(no_schtasks) != "0";
    ExecResult r;
    if (skip_schtasks) {
        r.exit_code = 1;
        r.err = "skipped via ICMG_SERVICE_NO_SCHTASKS";
    } else {
        r = safeExecShell(
            "MSYS_NO_PATHCONV=1 schtasks /Create /SC ONLOGON /TN \"icmg-service\""
            " /TR \"wscript.exe //B //Nologo \\\"" + vbs.string() + "\\\"\""
            " /F", true, 15000);
    }
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
            // v1.8.1: direct CreateProcess via safeExec(argv) — no shell
            // layer. Works from bash, cmd.exe, PowerShell uniformly. No
            // MSYS path-conv, no prefix syntax compat issue.
            std::vector<std::string> lnk_argv{"wscript.exe","//B","//Nologo",mklnk.string()};
            auto rlnk = safeExec(lnk_argv, true, 10000);
            std::error_code ec3;
            if (rlnk.exit_code == 0 && fs::exists(lnk, ec3)) {
                std::vector<std::string> boot_argv{"wscript.exe","//B","//Nologo",vbs.string()};
                (void)safeExec(boot_argv, true, 5000);
                return true;
            }
            // v1.6.6: surface wscript stderr to caller so root cause is
            // visible in `icmg init` output rather than just "fallback failed".
            if (rlnk.exit_code != 0 || !fs::exists(lnk, ec3)) {
                std::string detail = rlnk.err.empty() ? rlnk.out : rlnk.err;
                if (detail.empty()) detail = "wscript exit=" + std::to_string(rlnk.exit_code)
                                          + " lnk_exists=" + (fs::exists(lnk, ec3) ? "yes" : "no");
                // Trim trailing whitespace.
                while (!detail.empty() && (detail.back() == '\r' || detail.back() == '\n'
                                            || detail.back() == ' ')) detail.pop_back();
                setErr("Startup-folder fallback failed: " + detail
                       + ". Manual: copy " + vbs.string() + " shortcut to "
                       + "%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\");
                return false;
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
    // v1.11.0: POSIX systemd / launchd installer.
    //
    // Resolve absolute path to icmg binary.
    std::string icmg_path;
    {
        char buf[4096] = {0};
#  ifdef __APPLE__
        uint32_t sz = sizeof(buf);
        if (_NSGetExecutablePath(buf, &sz) == 0) icmg_path = buf;
#  else
        ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = 0; icmg_path = buf; }
#  endif
    }
    if (icmg_path.empty()) {
        const char* p = std::getenv("ICMG_BIN");
        icmg_path = (p && *p) ? p : "icmg";
    }

    const char* home_c = std::getenv("HOME");
    if (!home_c || !*home_c) {
        setErr("HOME not set; cannot install user service");
        return false;
    }
    fs::path home = home_c;
    std::error_code ec;

#  ifdef __APPLE__
    // macOS: launchd user-agent plist.
    fs::path agents = home / "Library" / "LaunchAgents";
    fs::create_directories(agents, ec);
    if (ec) { setErr("mkdir " + agents.string() + ": " + ec.message()); return false; }
    fs::path plist = agents / "com.icmg.service.plist";
    {
        std::ofstream f(plist);
        if (!f) { setErr("cannot write " + plist.string()); return false; }
        f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
          << " \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          << "<plist version=\"1.0\"><dict>\n"
          << "  <key>Label</key><string>com.icmg.service</string>\n"
          << "  <key>ProgramArguments</key><array>\n"
          << "    <string>" << icmg_path << "</string>\n"
          << "    <string>service</string>\n"
          << "    <string>run</string>\n"
          << "  </array>\n"
          << "  <key>RunAtLoad</key><true/>\n"
          << "  <key>KeepAlive</key><true/>\n"
          << "  <key>StandardErrorPath</key><string>"
          << (home / ".icmg" / "service.err.log").string() << "</string>\n"
          << "  <key>StandardOutPath</key><string>"
          << (home / ".icmg" / "service.out.log").string() << "</string>\n"
          << "</dict></plist>\n";
    }
    fs::create_directories(home / ".icmg", ec);
    // Load (idempotent: unload first, ignore error).
    (void)safeExecShell("launchctl unload " + plist.string() + " 2>/dev/null", false, 5000);
    auto r = safeExecShell("launchctl load " + plist.string(), false, 5000);
    if (r.exit_code != 0) {
        setErr("launchctl load failed (exit=" + std::to_string(r.exit_code)
               + "): " + (r.err.empty() ? r.out : r.err)
               + ". Plist installed at " + plist.string()
               + " — reload manually: launchctl load " + plist.string());
        return false;
    }
    return true;
#  else
    // Linux: systemd --user unit.
    fs::path unit_dir = home / ".config" / "systemd" / "user";
    fs::create_directories(unit_dir, ec);
    if (ec) { setErr("mkdir " + unit_dir.string() + ": " + ec.message()); return false; }
    fs::path unit = unit_dir / "icmg.service";
    {
        std::ofstream f(unit);
        if (!f) { setErr("cannot write " + unit.string()); return false; }
        f << "[Unit]\n"
          << "Description=icmg context graph service\n"
          << "After=network.target\n\n"
          << "[Service]\n"
          << "Type=simple\n"
          << "ExecStart=" << icmg_path << " service run\n"
          << "Restart=on-failure\n"
          << "RestartSec=10\n"
          << "StandardOutput=append:" << (home / ".icmg" / "service.out.log").string() << "\n"
          << "StandardError=append:"  << (home / ".icmg" / "service.err.log").string() << "\n\n"
          << "[Install]\n"
          << "WantedBy=default.target\n";
    }
    fs::create_directories(home / ".icmg", ec);
    auto rel = safeExecShell("systemctl --user daemon-reload", false, 5000);
    (void)rel;
    auto re = safeExecShell("systemctl --user enable icmg.service", false, 5000);
    auto rs = safeExecShell("systemctl --user start icmg.service", false, 5000);
    if (re.exit_code != 0 || rs.exit_code != 0) {
        setErr("systemd install partial — unit written to " + unit.string()
               + "; enable/start failed (likely no user-session bus). Manual: "
               + "systemctl --user daemon-reload && "
               + "systemctl --user enable --now icmg.service");
        // Unit on disk → still successful artifact. Return true so caller
        // proceeds; user can complete manually.
        return true;
    }
    return true;
#  endif
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
