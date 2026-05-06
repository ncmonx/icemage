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

    // Build command line with proper quoting
    std::string cmd;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmd += ' ';
        // Quote args containing spaces or special chars
        bool needs_quote = argv[i].find_first_of(" \t\"") != std::string::npos;
        if (needs_quote) cmd += '"';
        for (char c : argv[i]) {
            if (c == '"') cmd += "\\\"";
            else cmd += c;
        }
        if (needs_quote) cmd += '"';
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

} // namespace icmg::core
