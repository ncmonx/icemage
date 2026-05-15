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

    // 2) Register logon-trigger task (overwrite with /F).
    std::string cmd =
        "MSYS_NO_PATHCONV=1 schtasks /Create /SC ONLOGON /TN \"icmg-service\""
        " /TR \"wscript.exe //B //Nologo \\\"" + vbs.string() + "\\\"\""
        " /F";
    auto r = safeExecShell(cmd, true, 15000);
    if (r.exit_code != 0) {
        setErr("schtasks failed: " + r.err);
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
