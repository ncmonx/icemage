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

} // namespace icmg::core
