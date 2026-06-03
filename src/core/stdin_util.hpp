#pragma once
// Guarded stdin slurp. Reads all of stdin into a string, BUT returns empty
// immediately when stdin is an interactive TTY (no piped input). This prevents
// an indefinite block on `std::cin.rdbuf()` when a stdin-reading command is
// spawned without piped input — the root of icmg proc-accumulation hangs under
// hook-spawn burst (a hook fires `icmg hookio` etc. and the process blocks on
// stdin forever). Piped input (the normal hook path) is unaffected.
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#ifdef _WIN32
#include <io.h>
#define ICMG_STDIN_ISATTY() (_isatty(_fileno(stdin)) != 0)
#else
#include <unistd.h>
#define ICMG_STDIN_ISATTY() (isatty(fileno(stdin)) != 0)
#endif

namespace icmg::core {

inline std::string slurpStdinSafe() {
    if (ICMG_STDIN_ISATTY()) return "";  // interactive terminal -> no piped input, don't block
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

}  // namespace icmg::core
