#include "exec_utils.hpp"
#include <stdexcept>
#include <chrono>
#include <array>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/wait.h>
#  include <spawn.h>
#  include <poll.h>
extern char** environ;
#endif

namespace icmg::core {

namespace {
    auto now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

#ifdef _WIN32

static ExecResult safeExecWin(const std::vector<std::string>& argv,
                               bool merge_stderr, int timeout_ms) {
    ExecResult result;
    if (argv.empty()) { result.exit_code = -1; return result; }

    // MSYS-aware routing: when running under MSYS2/Git Bash and argv[0] is a
    // bare command name (no path, no .exe/.com), route through bash -lc so:
    //   - `find`/`sort`/`link`/`tee` resolve to MSYS bins, not Windows shadows
    //     (Windows find.exe is a text search tool, NOT GNU find).
    //   - `pnpm`/`npx`/`yarn` (.cmd shims) launch correctly via shell.
    //   - npm prefix and ~/.bashrc PATH augmentations apply.
    // Skip when argv[0] contains '/' or '\\' (explicit path) or has executable
    // extension (user already resolved binary).
    if (std::getenv("MSYSTEM") != nullptr || std::getenv("BASH") != nullptr) {
        const std::string& a0 = argv[0];
        bool has_path = a0.find('/') != std::string::npos || a0.find('\\') != std::string::npos;
        auto ends_with_ci = [](const std::string& s, const char* ext) {
            size_t n = std::strlen(ext);
            if (s.size() < n) return false;
            for (size_t i = 0; i < n; ++i)
                if (std::tolower((unsigned char)s[s.size()-n+i]) != ext[i]) return false;
            return true;
        };
        bool has_ext = ends_with_ci(a0, ".exe") || ends_with_ci(a0, ".com");
        if (!has_path && !has_ext) {
            // Reconstruct shell-quoted command line.
            std::string sh_cmd;
            for (size_t i = 0; i < argv.size(); ++i) {
                if (i) sh_cmd += ' ';
                const std::string& a = argv[i];
                bool need_quote = a.empty() || a.find_first_of(" \t\"'$`\\|&;<>()*?#~!") != std::string::npos;
                if (!need_quote) { sh_cmd += a; continue; }
                sh_cmd += '\'';
                for (char c : a) {
                    if (c == '\'') sh_cmd += "'\\''";
                    else sh_cmd += c;
                }
                sh_cmd += '\'';
            }
            return safeExecShell(sh_cmd, merge_stderr, timeout_ms);
        }
    }

    // Build command line using Windows CommandLineToArgvW-compatible quoting.
    //
    // Rules (from Raymond Chen / MSDN):
    //   - Backslashes are literal UNLESS followed by a double-quote.
    //   - 2N backslashes before a literal " → N backslashes + escaped "
    //   - 2N+1 backslashes before a literal " → N backslashes + opening/closing "
    //   - Trailing backslashes in a quoted arg must be doubled.
    std::string cmd;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmd += ' ';

        const std::string& arg = argv[i];
        bool needs_quote = arg.empty() ||
                           arg.find_first_of(" \t\"") != std::string::npos;

        if (!needs_quote) {
            cmd += arg;
            continue;
        }

        cmd += '"';
        size_t j = 0;
        while (j < arg.size()) {
            // Count consecutive backslashes
            size_t bs_count = 0;
            while (j < arg.size() && arg[j] == '\\') { ++j; ++bs_count; }

            if (j == arg.size()) {
                // Trailing backslashes — double them before closing quote
                for (size_t k = 0; k < bs_count * 2; ++k) cmd += '\\';
            } else if (arg[j] == '"') {
                // Backslashes before a quote — double them + escape the quote
                for (size_t k = 0; k < bs_count * 2 + 1; ++k) cmd += '\\';
                cmd += '"';
                ++j;
            } else {
                // Ordinary backslashes — literal
                for (size_t k = 0; k < bs_count; ++k) cmd += '\\';
                cmd += arg[j++];
            }
        }
        cmd += '"';
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hOutR, hOutW, hErrR, hErrW;
    CreatePipe(&hOutR, &hOutW, &sa, 0);
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);

    if (merge_stderr) {
        hErrR = nullptr;
        DuplicateHandle(GetCurrentProcess(), hOutW, GetCurrentProcess(),
                        &hErrW, 0, TRUE, DUPLICATE_SAME_ACCESS);
    } else {
        CreatePipe(&hErrR, &hErrW, &sa, 0);
        SetHandleInformation(hErrR, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.hStdOutput = hOutW;
    si.hStdError  = hErrW;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    auto t0 = now_ms();

    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    bool ok = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr,
                              TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                              &si, &pi);
    CloseHandle(hOutW);
    CloseHandle(hErrW);

    if (!ok) {
        DWORD err = GetLastError();
        CloseHandle(hOutR);
        if (!merge_stderr) CloseHandle(hErrR);
        // ERROR_FILE_NOT_FOUND (2) for bare commands → likely MSYS builtin
        // (`find`, `grep`, `awk`) or .cmd/.bat shim (`pnpm.cmd`, `npx.cmd`,
        // `yarn.cmd`) which CreateProcess cannot launch directly. Retry via
        // bash -lc when running under MSYS2/Git Bash so login profile loads
        // /usr/bin + ~/.bashrc PATH augmentations (npm prefix etc.).
        // Retry on ANY Windows (not just MSYS): safeExecShell picks bash under
        // MSYS, else full-path PowerShell -- so builtins/cmdlets (echo, dir,
        // Get-ChildItem) and .cmd/.bat shims resolve where CreateProcess can't.
        if (err == ERROR_FILE_NOT_FOUND) {
            // Reconstruct shell-quoted command line.
            std::string sh_cmd;
            for (size_t i = 0; i < argv.size(); ++i) {
                if (i) sh_cmd += ' ';
                const std::string& a = argv[i];
                bool need_quote = a.empty() || a.find_first_of(" \t\"'$`\\|&;<>()*?#~!") != std::string::npos;
                if (!need_quote) { sh_cmd += a; continue; }
                sh_cmd += '\'';
                for (char c : a) {
                    if (c == '\'') sh_cmd += "'\\''";  // close, escape, reopen
                    else sh_cmd += c;
                }
                sh_cmd += '\'';
            }
            return safeExecShell(sh_cmd, merge_stderr, timeout_ms);
        }
        result.exit_code = -1;
        result.err = "CreateProcess failed: " + std::to_string(err);
        return result;
    }

    // Read stdout
    std::array<char, 4096> buf;
    DWORD bytes;
    while (ReadFile(hOutR, buf.data(), (DWORD)buf.size(), &bytes, nullptr) && bytes)
        result.out.append(buf.data(), bytes);

    if (!merge_stderr) {
        while (ReadFile(hErrR, buf.data(), (DWORD)buf.size(), &bytes, nullptr) && bytes)
            result.err.append(buf.data(), bytes);
        CloseHandle(hErrR);
    }
    CloseHandle(hOutR);

    WaitForSingleObject(pi.hProcess, (DWORD)timeout_ms);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = (int)exit_code;
    result.duration_ms = now_ms() - t0;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}

ExecResult safeExec(const std::vector<std::string>& argv,
                    bool merge_stderr, int timeout_ms) {
    return safeExecWin(argv, merge_stderr, timeout_ms);
}

#else // Unix

ExecResult safeExec(const std::vector<std::string>& argv,
                    bool merge_stderr, int timeout_ms) {
    ExecResult result;
    if (argv.empty()) return result;

    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0) { result.exit_code = -1; return result; }
    if (!merge_stderr) {
        if (pipe(err_pipe) != 0) { result.exit_code = -1; return result; }
    }

    auto t0 = now_ms();
    pid_t pid;

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, out_pipe[1], STDOUT_FILENO);
    if (merge_stderr) {
        posix_spawn_file_actions_adddup2(&fa, out_pipe[1], STDERR_FILENO);
    } else {
        posix_spawn_file_actions_adddup2(&fa, err_pipe[1], STDERR_FILENO);
    }
    posix_spawn_file_actions_addclose(&fa, out_pipe[0]);
    posix_spawn_file_actions_addclose(&fa, out_pipe[1]);
    if (!merge_stderr) {
        posix_spawn_file_actions_addclose(&fa, err_pipe[0]);
        posix_spawn_file_actions_addclose(&fa, err_pipe[1]);
    }

    // Build argv array for posix_spawn
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
    cargv.push_back(nullptr);

    int ret = posix_spawnp(&pid, cargv[0], &fa, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);

    close(out_pipe[1]);
    if (!merge_stderr) close(err_pipe[1]);

    if (ret != 0) {
        close(out_pipe[0]);
        if (!merge_stderr) close(err_pipe[0]);
        result.exit_code = -1;
        return result;
    }

    // Read with poll
    std::array<char, 4096> buf;
    struct pollfd fds[2];
    int nfds = 1;
    fds[0] = {out_pipe[0], POLLIN, 0};
    if (!merge_stderr) { fds[1] = {err_pipe[0], POLLIN, 0}; nfds = 2; }

    bool out_done = false, err_done = merge_stderr;
    while (!out_done || !err_done) {
        int r = poll(fds, nfds, timeout_ms);
        if (r <= 0) break;
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            ssize_t n = read(out_pipe[0], buf.data(), buf.size());
            if (n <= 0) out_done = true;
            else result.out.append(buf.data(), n);
        }
        if (!merge_stderr && (fds[1].revents & (POLLIN | POLLHUP))) {
            ssize_t n = read(err_pipe[0], buf.data(), buf.size());
            if (n <= 0) err_done = true;
            else result.err.append(buf.data(), n);
        }
    }

    close(out_pipe[0]);
    if (!merge_stderr) close(err_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.duration_ms = now_ms() - t0;
    return result;
}

#endif // _WIN32

// ---------------------------------------------------------------------------
// safeExecShell — pass-through to shell, bypassing argv-quoting machinery.
// Needed for `icmg parallel --task "<cmd>"` etc. where the command string is
// a full shell line (paths-with-spaces, pipes, &&) that must NOT be re-tokenized.
// ---------------------------------------------------------------------------
ExecResult safeExecShell(const std::string& cmd_line, bool merge_stderr, int timeout_ms) {
#ifdef _WIN32
    ExecResult result;
    if (cmd_line.empty()) { result.exit_code = -1; return result; }

    // Pattern: command starts with a quoted absolute path → bypass cmd.exe.
    // cmd.exe's pathname resolver mishandles paths containing dot-prefixed
    // folders like `C:\Users\X\.local\bin\app.exe` even with /s /c quoting,
    // emitting "is not recognized as ... command".
    // Workaround: when first token is a quoted absolute path, call
    // CreateProcessA with lpApplicationName set explicitly — Windows uses
    // the path as-is without parsing. Shell features (pipes, &&, redirects)
    // are NOT supported in this fast path; we detect them and fall through.
    bool use_direct = false;
    std::string app_name;
    std::string cmd_for_direct;
    if (cmd_line.size() > 4 && cmd_line[0] == '"') {
        size_t end_quote = cmd_line.find('"', 1);
        if (end_quote != std::string::npos && end_quote > 3) {
            std::string candidate = cmd_line.substr(1, end_quote - 1);
            // Absolute path? "X:\..." or "X:/..."
            bool absolute = candidate.size() >= 3 && candidate[1] == ':' &&
                            (candidate[2] == '\\' || candidate[2] == '/');
            // No shell metacharacters in the rest of the line
            std::string tail = cmd_line.substr(end_quote + 1);
            bool has_shell = tail.find('|') != std::string::npos ||
                             tail.find('>') != std::string::npos ||
                             tail.find('<') != std::string::npos ||
                             tail.find('&') != std::string::npos ||
                             tail.find(';') != std::string::npos;
            if (absolute && !has_shell) {
                use_direct = true;
                app_name = candidate;
                cmd_for_direct = cmd_line;  // keep quotes — CreateProcessA expects it
            }
        }
    }

    // cmd.exe /s /c "<command>" preserves all internal quotes verbatim
    // (the /s flag strips ONLY the first and last char if both are ").
    //
    // Prefer bash on MSYS — when running under MSYS2/MinGW (or Git Bash),
    // PATH-resolution and shell builtins (cat, head, grep, awk) match the
    // calling shell. cmd.exe loses this when env paths are MSYS-formatted
    // (`/c/...`) instead of `C:\...` — yields "CreateProcess failed: 2"
    // even when binary IS on PATH from bash perspective.
    std::string full_cmd;
    if (use_direct) {
        full_cmd = cmd_for_direct;  // direct exec, no shell wrap
    } else {
        bool prefer_bash = std::getenv("MSYSTEM") != nullptr
                        || std::getenv("BASH") != nullptr;
        std::string bash_path;
        if (prefer_bash) {
            // Resolve bash.exe by checking common locations; PATH lookup
            // through cmd.exe wouldn't help us at this point.
            const char* candidates[] = {
                "C:/msys64/usr/bin/bash.exe",
                "C:/msys64/mingw64/bin/bash.exe",
                "C:/Program Files/Git/bin/bash.exe",
                "C:/Program Files/Git/usr/bin/bash.exe",
                nullptr
            };
            for (int i = 0; candidates[i]; ++i) {
                std::ifstream f(candidates[i]);
                if (f.good()) { bash_path = candidates[i]; break; }
            }
            if (bash_path.empty()) prefer_bash = false;
        }
        if (prefer_bash) {
            // bash -c with PATH prepended — keep parent's PATH (so Windows-
            // installed tools like git/node/etc. stay reachable) but prepend
            // MSYS bin dirs so `find`/`grep`/`awk`/`sort` resolve to GNU
            // versions, not Windows shadows.
            // Avoid -l (login): would source /etc/profile and OVERRIDE PATH
            // to MSYS-only, breaking tools the user added via Windows install.
            std::string esc = cmd_line;
            std::string out;
            out.reserve(esc.size() + 16);
            for (char c : esc) {
                if (c == '"' || c == '\\') out.push_back('\\');
                out.push_back(c);
            }
            // Inject PATH prefix inside bash invocation. Using single-quoted
            // here-prefix so $PATH expands inside child bash to its inherited
            // value. No profile load → low latency.
            full_cmd = "\"" + bash_path + "\" -c \""
                       "export PATH=\\\"/usr/bin:/mingw64/bin:$PATH\\\"; "
                       + out + "\"";
        } else {
            // Prefer pwsh (PS7+) or powershell (PS5) over cmd.exe on non-MSYS Windows.
            // Enables Select-String, Get-ChildItem, etc. without MSYS install.
            // Checked once per process: pwsh.exe first, fall back to powershell.exe.
            static std::string s_shell;
            if (s_shell.empty()) {
                auto envOr = [](const char* k, const char* def) {
                    const char* v = std::getenv(k); std::string s = v ? v : def;
                    for (char& ch : s) if (ch == '\\') ch = '/';
                    return s;
                };
                std::string pf = envOr("ProgramFiles", "C:/Program Files");
                std::string sr = envOr("SystemRoot",   "C:/Windows");
                std::vector<std::string> cands = {
                    pf + "/PowerShell/7/pwsh.exe",
                    pf + "/PowerShell/7.5/pwsh.exe",
                    pf + "/PowerShell/7.4/pwsh.exe",
                };
                std::string ps5 = sr + "/System32/WindowsPowerShell/v1.0/powershell.exe";
                s_shell = resolveWinShell(cands, ps5,
                    [](const std::string& p){ std::ifstream f(p); return f.good(); });
            }
            // Escape inner double-quotes for the -Command "<...>" wrapper.
            std::string esc;
            esc.reserve(cmd_line.size() + 16);
            for (char ch : cmd_line) {
                if (ch == '"') esc += "\\\"";
                else esc += ch;
            }
            // Quote the FULL shell path so CreateProcessA uses it verbatim (no
            // PATH search) -- fixes "CreateProcess failed: 2" on non-MSYS Windows
            // (PS5 lives in a System32 subdir, Store pwsh is an app alias).
            full_cmd = "\"" + s_shell + "\" -NoProfile -NonInteractive -Command \"" + esc + "\"";        }
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hOutR, hOutW, hErrR = nullptr, hErrW;
    CreatePipe(&hOutR, &hOutW, &sa, 0);
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
    if (merge_stderr) {
        DuplicateHandle(GetCurrentProcess(), hOutW, GetCurrentProcess(),
                        &hErrW, 0, TRUE, DUPLICATE_SAME_ACCESS);
    } else {
        CreatePipe(&hErrR, &hErrW, &sa, 0);
        SetHandleInformation(hErrR, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.hStdOutput = hOutW;
    si.hStdError  = hErrW;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    auto t0 = now_ms();

    std::vector<char> cmdBuf(full_cmd.begin(), full_cmd.end());
    cmdBuf.push_back('\0');

    // When use_direct: pass app_name as lpApplicationName so Windows skips
    // its path-parsing heuristics (which choke on .dotfolder segments).
    LPCSTR lp_app = use_direct ? app_name.c_str() : nullptr;
    bool ok = CreateProcessA(lp_app, cmdBuf.data(), nullptr, nullptr,
                              TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                              &si, &pi);
    CloseHandle(hOutW);
    CloseHandle(hErrW);

    if (!ok) {
        result.exit_code = -1;
        result.err = "CreateProcess failed: " + std::to_string(GetLastError());
        CloseHandle(hOutR);
        if (!merge_stderr) CloseHandle(hErrR);
        return result;
    }

    // init_cmd.cpp background imports now use CreateProcessA(bInheritHandles=FALSE),
    // so no grandchild inherits this pipe. Original ReadFile loop restored.
    std::array<char, 4096> buf;
    DWORD bytes;
    while (ReadFile(hOutR, buf.data(), (DWORD)buf.size(), &bytes, nullptr) && bytes)
        result.out.append(buf.data(), bytes);
    if (!merge_stderr) {
        while (ReadFile(hErrR, buf.data(), (DWORD)buf.size(), &bytes, nullptr) && bytes)
            result.err.append(buf.data(), bytes);
        CloseHandle(hErrR);
    }
    CloseHandle(hOutR);

    WaitForSingleObject(pi.hProcess, (DWORD)timeout_ms);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = (int)exit_code;
    result.duration_ms = now_ms() - t0;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
#else
    // POSIX: argv-style is already correct (sh -c receives the whole string
    // as a single argument). Reuse safeExec.
    return safeExec({"/bin/sh", "-c", cmd_line}, merge_stderr, timeout_ms);
#endif
}

} // namespace icmg::core

