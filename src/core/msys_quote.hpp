#pragma once
// Child-flavor-aware cmdline quoting for CreateProcess (wflog #16755).
// Ported from a companion agent harness's verified MSYS/MSVCRT quoting rules.
//
// CreateProcess passes ONE command-line string; the CHILD re-splits it with
// its own runtime's rules:
//   - Native Windows children (MSVCRT / CommandLineToArgvW): backslashes are
//     literal UNLESS they precede a double quote.
//   - MSYS/Cygwin children (Git Bash usr/bin tools: bash, grep, sed, find):
//     inside double quotes `\\` -> `\` and `\"` -> `"`.
// Quoting an MSYS child's args MSVCRT-style therefore loses one backslash of
// every pair. buildChildCmdline() picks the right rule per argv[0].

#include <cctype>
#include <string>
#include <vector>

namespace icmg::core {

// MSVCRT / CommandLineToArgvW quoting (Raymond Chen / MSDN rules):
//   2N   backslashes before `"` -> N backslashes + escaped quote
//   trailing backslashes in a quoted arg must be doubled
inline std::string quoteArgMsvcrt(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \t\n\v\"") == std::string::npos)
        return a;
    std::string out = "\"";
    size_t j = 0;
    while (j < a.size()) {
        size_t bs = 0;
        while (j < a.size() && a[j] == '\\') { ++j; ++bs; }
        if (j == a.size()) {
            out.append(bs * 2, '\\');              // trailing: double them
        } else if (a[j] == '"') {
            out.append(bs * 2 + 1, '\\');          // double + escape the quote
            out += '"';
            ++j;
        } else {
            out.append(bs, '\\');                  // ordinary: literal
            out += a[j++];
        }
    }
    out += '"';
    return out;
}

// MSYS/Cygwin quoting: inside double quotes every `\` and `"` is escaped
// with a single backslash (`\\` -> `\`, `\"` -> `"` on the child side).
inline std::string quoteArgMsys(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \t\n\v\"\\") == std::string::npos)
        return a;
    std::string out = "\"";
    for (char c : a) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

// Heuristic: is this executable an MSYS/Cygwin program (parses its cmdline
// with MSYS rules)? Matches Git-for-Windows + MSYS2 install layouts and bare
// names of well-known MSYS shells/tools. `C:\Windows\System32\bash.exe` (the
// WSL shim) is NATIVE and must stay MSVCRT.
inline bool isMsysProgram(const std::string& exe) {
    std::string s;
    s.reserve(exe.size());
    for (char c : exe) s += (c == '\\') ? '/' : (char)std::tolower((unsigned char)c);
    if (s.find("/windows/system32/") != std::string::npos) return false;
    if (s.find("msys") != std::string::npos || s.find("cygwin") != std::string::npos)
        return true;
    if (s.find("/git/usr/bin/") != std::string::npos ||
        s.find("/git/bin/")     != std::string::npos ||
        s.find("/git/mingw64/bin/") != std::string::npos)
        return true;
    // Bare well-known MSYS shells (resolved via PATH to Git Bash in practice).
    auto slash = s.find_last_of('/');
    std::string base = (slash == std::string::npos) ? s : s.substr(slash + 1);
    if (base.size() > 4 && base.compare(base.size() - 4, 4, ".exe") == 0)
        base.erase(base.size() - 4);
    return base == "bash" || base == "sh" || base == "dash" || base == "zsh";
}

// Join argv into ONE CreateProcess command line, quoting each arg with the
// rule the CHILD (argv[0]) will use to re-split it.
inline std::string buildChildCmdline(const std::vector<std::string>& argv) {
    const bool msys = !argv.empty() && isMsysProgram(argv[0]);
    std::string cmd;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmd += ' ';
        if (i == 0) cmd += quoteArgMsvcrt(argv[i]);  // path itself: MSVCRT
        else        cmd += msys ? quoteArgMsys(argv[i]) : quoteArgMsvcrt(argv[i]);
    }
    return cmd;
}

}  // namespace icmg::core
