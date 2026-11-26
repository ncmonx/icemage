#pragma once
#include "../core/exec_utils.hpp"
#include <string>
#include <vector>

namespace icmg::tkil {

// Parse a shell-style command string into argv tokens.
// Respects "quoted strings" and 'single quotes'.
std::vector<std::string> parseArgv(const std::string& command);

// Quote one argv token for a POSIX shell line that parseArgv can also invert.
// Any shell metachar forces "..." quoting; inside it `"` `\` `$` and backtick
// are backslash-escaped (bash expands `$`/backtick even inside "..." -- #295,
// a `bash -c "<script>"` token got its script run by the OUTER shell). Plain
// tokens stay bare (no churn). Inverse: parseArgv (round-trip tested).
inline constexpr const char* kShellQuoteTriggers = " \t\n\"\\'$`|&;<>()*?#~!{}[]";

inline std::string shellQuoteArg(const std::string& a) {
    if (a.empty()) return "\"\"";
    if (a.find_first_of(kShellQuoteTriggers) == std::string::npos) return a;
    std::string out = "\"";
    for (char c : a) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') out += '\\';
        out += c;
    }
    out += "\"";
    return out;
}

// True if `command` contains an UNQUOTED shell operator (pipe, redirect,
// &&/||/;, background &, backtick, $(...)) -- i.e. it needs a real shell, not
// an argv exec. Operators inside '...' or "..." are literal and do NOT count
// (so `grep 'a|b' f` stays on the argv-safe path). Pure -> unit-testable.
inline bool hasShellOperators(const std::string& cmd) {
    bool in_s = false, in_d = false;
    for (size_t i = 0; i < cmd.size(); ++i) {
        char c = cmd[i];
        if (in_s) { if (c == '\'') in_s = false; continue; }
        if (in_d) {
            // #295: inside "..." a backslash escapes the next char -- `\"` must
            // not close the quote (else the rest of the string looked unquoted
            // and its backticks sent the whole line to an outer shell).
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_d = false;
            continue;
        }
        if (c == '\'') { in_s = true; continue; }
        if (c == '"')  { in_d = true; continue; }
        if (c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '`')
            return true;
        if (c == '$' && i + 1 < cmd.size() && cmd[i + 1] == '(') return true;
    }
    return false;
}

// Execute a command. Commands with unquoted shell operators (pipes, redirects,
// &&/;) route through safeExecShell so the shell interprets them; simple
// commands take the argv-safe path (parseArgv + safeExec, no shell parsing,
// no injection surface). Fixes `icmg run "ls | grep x"` where the pipe was
// previously split into literal argv tokens fed to `ls`.
// `icmg run --stream` is meant for long jobs: its wait cap is a day, not the
// buffered run's 60 s (POSIX: max silence between chunks).
inline constexpr int kStreamTimeoutMs = 24 * 60 * 60 * 1000;

inline core::ExecResult runCommand(const std::string& command,
                                   bool merge_stderr = true,
                                   int timeout_ms = 60000) {
    if (hasShellOperators(command))
        return core::safeExecShell(command, merge_stderr, timeout_ms);
    auto argv = parseArgv(command);
    if (argv.empty()) return {};
    return core::safeExec(argv, merge_stderr, timeout_ms);
}

} // namespace icmg::tkil
