#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace icmg::core {

struct ExecResult {
    std::string out;
    std::string err;
    int         exit_code   = -1;
    int64_t     duration_ms = 0;
};

// Safe command execution — argv array, NO shell intermediary.
// On Windows: uses CreateProcess. On Unix: uses posix_spawn/fork+execvp.
// Never passes argv through system() or popen(string).
ExecResult safeExec(const std::vector<std::string>& argv,
                    bool merge_stderr = false,
                    int timeout_ms    = 30000);

// Shell command execution — pass-through via /bin/sh -c (Unix) or cmd.exe /s /c (Win).
// Use ONLY when running a user-supplied command string with shell features
// (pipes, redirects, &&, ||, glob expansion, paths with spaces). Bypasses
// argv-quoting that conflicts with cmd.exe's own parser.
//
// Security: cmd_line is run through the shell — caller is responsible for
// trusting / sanitizing the input. Never pass external user input here.
ExecResult safeExecShell(const std::string& cmd_line,
                         bool merge_stderr = false,
                         int timeout_ms    = 30000);

// M10: curl binary name. On Windows under PowerShell, bare  is an alias
// M10: curl binary name. On Windows under PowerShell, bare `curl` is an alias
// for Invoke-WebRequest (not curl.exe) -> wrong output. Use `curl.exe` to bypass.
inline const char* curlBin() {
#ifdef _WIN32
    return "curl.exe";
#else
    return "curl";
#endif
}

// Resolve the non-MSYS Windows shell to a FULL absolute path. CreateProcessA's
// PATH search (NULL lpApplicationName) is unreliable here: it misses
// powershell.exe under System32\WindowsPowerShell\v1.0 and the Store/WindowsApps
// pwsh app-execution alias, yielding "CreateProcess failed: 2" for users without
// git-bash. Pure + templated on the existence predicate so it is unit-testable.
// Returns the first existing pwsh candidate, else the PowerShell 5 full path.
template <typename ExistsFn>
inline std::string resolveWinShell(const std::vector<std::string>& candPwsh,
                                   const std::string& powershell5Full,
                                   ExistsFn exists) {
    for (const auto& c : candPwsh) if (!c.empty() && exists(c)) return c;
    return powershell5Full;
}
// Append a bash-safe stderr suppression to a shell command destined for
// safeExecShell(). 2026-07-16: safeExecShell() PREFERS bash (Git Bash / MSYS2)
// whenever bash.exe is found, even on Windows -- so a Windows-style `2>nul`
// redirect does NOT hit the null device there; bash treats `nul` as an
// ordinary relative filename and creates a stray `nul` FILE in the current
// working directory (the 2026-07-16 stray-nul bug). `/dev/null` is understood
// by bash on every platform (Git Bash/MSYS provide it on Windows), so it is
// the correct sink for any command routed through safeExecShell. Use this
// helper instead of hand-writing `2>nul` / `>nul` in a safeExecShell string.
inline std::string suppressStderr(const std::string& cmd) {
    return cmd + " 2>/dev/null";
}

} // namespace icmg::core
