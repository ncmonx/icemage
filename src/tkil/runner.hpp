#pragma once
#include "../core/exec_utils.hpp"
#include <string>
#include <vector>

namespace icmg::tkil {

// Parse a shell-style command string into argv tokens.
// Respects "quoted strings" and 'single quotes'.
std::vector<std::string> parseArgv(const std::string& command);

// Thin wrapper — uses core::safeExec (no shell injection possible).
inline core::ExecResult runCommand(const std::string& command,
                                   bool merge_stderr = true,
                                   int timeout_ms = 60000) {
    auto argv = parseArgv(command);
    if (argv.empty()) return {};
    return core::safeExec(argv, merge_stderr, timeout_ms);
}

} // namespace icmg::tkil
