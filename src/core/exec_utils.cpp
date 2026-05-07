#include "exec_utils.hpp"
#include <stdexcept>
#include <chrono>
#include <array>

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
    si.dwFlags    = STARTF_USESTDHANDLES;

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
        result.exit_code = -1;
        result.err = "CreateProcess failed: " + std::to_string(GetLastError());
        CloseHandle(hOutR);
        if (!merge_stderr) CloseHandle(hErrR);
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
    std::string full_cmd;
    if (use_direct) {
        full_cmd = cmd_for_direct;  // direct exec, no shell wrap
    } else {
        full_cmd = "cmd.exe /s /c \"";
        full_cmd += cmd_line;
        full_cmd += "\"";
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
    si.dwFlags    = STARTF_USESTDHANDLES;

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
