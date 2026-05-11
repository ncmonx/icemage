#include "schedule_helper.hpp"
#include "exec_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
  #include <windows.h>
#endif

namespace fs = std::filesystem;

namespace icmg::core {

// Look up the running binary's path. File-local helper.
static std::string selfExePath() {
#ifdef _WIN32
    char buf[1024];
    DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) return std::string(buf, n);
    return "icmg";
#else
    try { return fs::canonical("/proc/self/exe").string(); }
    catch (...) { return "icmg"; }
#endif
}

bool writeWrapperCmd(const std::string& wrapper_path,
                     const std::string& project_root,
                     const std::string& icmg_cmd,
                     const std::string& log_relpath) {
    fs::create_directories(fs::path(wrapper_path).parent_path());
    std::ofstream f(wrapper_path, std::ios::binary);
    if (!f) return false;
    // Windows line endings for cmd.exe.
    f << "@echo off\r\n"
      << "cd /d \"" << project_root << "\"\r\n"
      << "echo === %DATE% %TIME% " << icmg_cmd
      << " ===>> " << log_relpath << "\r\n"
      << "\"" << selfExePath() << "\" " << icmg_cmd
      << " >> " << log_relpath << " 2>&1\r\n";
    return true;
}

#ifdef _WIN32

static std::string scheduleSpecToSchtasksFlags(int minutes) {
    // Pick best schedule type.
    if (minutes >= 1440 && (minutes % 1440) == 0) {
        return "/SC DAILY /MO " + std::to_string(minutes / 1440);
    } else if (minutes >= 60 && (minutes % 60) == 0) {
        return "/SC HOURLY /MO " + std::to_string(minutes / 60);
    } else {
        return "/SC MINUTE /MO " + std::to_string(minutes);
    }
}

int registerWindowsSchedule(const ScheduleSpec& spec) {
    if (spec.wrapper_path.empty() || spec.task_name.empty()) return 1;

    std::string sched = scheduleSpecToSchtasksFlags(spec.minutes);

    // First attempt: direct schtasks via bash w/ MSYS_NO_PATHCONV.
    // /TR points to wrapper as single-arg quoted path — no nested quotes.
    std::string cmd = "MSYS_NO_PATHCONV=1 schtasks /Create " + sched
                    + " /TN \"" + spec.task_name + "\""
                    + " /TR \"\\\"" + spec.wrapper_path + "\\\"\""
                    + " /F";
    auto res = safeExecShell(cmd, true, 15000);

    if (res.exit_code == 0) {
        std::cout << "  [+] " << spec.label << " auto-on: registered (every "
                  << spec.minutes << " min)\n";
        return 0;
    }

    // Diagnose error.
    std::string err = res.out.empty() ? res.err : res.out;
    bool denied = err.find("denied") != std::string::npos
               || err.find("Access") != std::string::npos
               || err.find("akses") != std::string::npos;

    if (!denied) {
        // Hard error — not elevation related. Print real message.
        std::cerr << "  [!] " << spec.label << " auto-on failed:\n    " << err;
        return 2;
    }

    // Elevation required → try PowerShell -Verb RunAs.
    std::cerr << "  [i] " << spec.label
              << " — elevation needed; prompting UAC...\n";

    // Build the schtasks argument list as a PowerShell array literal.
    // Each entry single-quoted (PowerShell). Inner double-quoted paths
    // preserved by PowerShell verbatim when -ArgumentList is used.
    std::ostringstream args;
    args << "'/Create',";
    // sched is "/SC X /MO N" — split by space into separate args.
    {
        std::istringstream is(sched);
        std::string tok;
        bool first = true;
        while (is >> tok) {
            if (!first) args << ",";
            first = false;
            args << "'" << tok << "'";
        }
    }
    args << ",'/TN','" << spec.task_name
         << "','/TR','\"" << spec.wrapper_path << "\"',"
         << "'/F'";

    std::string ps_cmd =
        "powershell -NoProfile -Command \""
        "Start-Process -FilePath schtasks -ArgumentList "
        + args.str()
        + " -Verb RunAs -Wait -WindowStyle Hidden\"";

    auto r2 = safeExecShell(ps_cmd, true, 60000);

    // Verify regardless of r2.exit_code — RunAs can return 0 even on user-cancel.
    std::string q = "MSYS_NO_PATHCONV=1 schtasks /Query /TN \""
                  + spec.task_name + "\"";
    auto verify = safeExecShell(q, false, 5000);

    if (verify.exit_code == 0) {
        std::cout << "  [+] " << spec.label
                  << " auto-on: registered via elevation (every "
                  << spec.minutes << " min)\n";
        return 0;
    }

    // Final fallback: manual instructions.
    std::cerr << "  [!] " << spec.label
              << " auto-on: elevation declined or failed.\n"
              << "    Manual setup (run elevated cmd.exe):\n"
              << "      schtasks /Create " << sched
              << " /TN \"" << spec.task_name << "\""
              << " /TR \"\\\"" << spec.wrapper_path << "\\\"\""
              << " /F\n";
    return 3;
}

int unregisterWindowsSchedule(const std::string& task_name) {
    std::string cmd = "MSYS_NO_PATHCONV=1 schtasks /Delete /TN \""
                    + task_name + "\" /F";
    auto res = safeExecShell(cmd, true, 5000);
    return res.exit_code;
}

bool queryWindowsSchedule(const std::string& task_name, std::string* status_out) {
    std::string cmd = "MSYS_NO_PATHCONV=1 schtasks /Query /TN \""
                    + task_name + "\" /FO LIST 2>nul";
    auto res = safeExecShell(cmd, false, 5000);
    if (res.exit_code != 0) return false;
    if (status_out) *status_out = res.out;
    return true;
}

#else // POSIX — placeholders. Each command keeps its existing crontab impl.

int registerWindowsSchedule(const ScheduleSpec&) { return 0; }
int unregisterWindowsSchedule(const std::string&) { return 0; }
bool queryWindowsSchedule(const std::string&, std::string*) { return false; }

#endif

} // namespace icmg::core
