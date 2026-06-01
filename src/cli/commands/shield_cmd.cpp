// v1.5.4: `icmg shield -- <argv...>` — SEM gatekeeper for child processes.
//
// Win32 child processes inherit the parent's error mode unless the parent
// passed CREATE_DEFAULT_ERROR_MODE. So if a process chain ever has a node
// that called SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX),
// all descendants silently swallow the "B:/ — system cannot find drive"
// dialog instead of popping it.
//
// Claude Code launches Stop / PostToolUse / SessionStart hooks via its own
// shell. That shell + its bash/jq/git children do NOT have SEM set, so any
// stray MSYS path translation to `B:\` triggers the popup. `icmg shield`
// inserts an SEM-setting node at the top of the hook chain:
//
//   Claude → cmd.exe → icmg.exe (SEM set) → execvp(<argv>) → bash → jq/git
//
// Once icmg.exe execs the target, its SEM persists in the now-bash process
// image, and bash's children inherit it.
//
// Usage:
//   icmg shield -- <command> [args...]
//   icmg shield <command> [args...]      (-- optional)
//
// Exit code: passes through child exit code on POSIX; on Win32 with execvp
// the parent process is replaced, so the OS reports the child's exit code
// to Claude directly.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <process.h>     // _execvp
#  include <windows.h>
#else
#  include <unistd.h>      // execvp
#endif

namespace icmg::cli {

class ShieldCommand : public BaseCommand {
public:
    std::string name()        const override { return "shield"; }
    std::string description() const override {
        return "SEM gatekeeper: set Win32 SetErrorMode then exec child (B:/ popup fix)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg shield [--] <command> [args...]\n\n"
            "Sets Win32 SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX in this\n"
            "process, then execvp's <command> in place. All descendants inherit\n"
            "the error mode and silently swallow drive-not-found popups.\n\n"
            "On POSIX, SEM is a no-op; only the exec happens.\n\n"
            "Example:\n"
            "  icmg shield -- bash .claude/hooks/icmg-cap-output.sh\n"
            "  icmg shield bash -c \"INPUT=$(cat); echo \\\"$INPUT\\\" | jq ...\"\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }

        // Strip leading '--' if present.
        std::vector<std::string> rest;
        rest.reserve(args.size());
        bool skip_sep = !args.empty() && args[0] == "--";
        for (size_t i = (skip_sep ? 1 : 0); i < args.size(); ++i)
            rest.push_back(args[i]);

        if (rest.empty()) {
            std::cerr << "icmg shield: missing <command>\n";
            usage();
            return 1;
        }

#ifdef _WIN32
        // SEM is already set by main.cpp at process entry (SEM_FAILCRITICALERRORS
        // | SEM_NOOPENFILEERRORBOX). Re-assert here for clarity — main.cpp set
        // is process-wide and persists across execvp on Windows.
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
#endif

        // Build C-style argv. Note: execvp on Win32 replaces the current
        // process IMAGE, but Windows internally creates a new process and the
        // parent (this icmg.exe) is killed once exec succeeds. The new image
        // inherits the error mode we just (re-)set.
        std::vector<char*> argv;
        argv.reserve(rest.size() + 1);
        for (auto& s : rest) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);

#ifdef _WIN32
        // _execvp searches PATH for the command. Returns -1 on failure, never
        // returns on success. Pre-set SEM persists across the _execvp transition
        // because Windows _execvp creates a new process inheriting the parent's
        // error mode (default Win32 child inheritance).
        int rc = _execvp(argv[0], argv.data());
        // Only reached on failure.
        std::cerr << "icmg shield: execvp failed for '" << rest[0]
                  << "' (errno=" << errno << ")\n";
        return rc < 0 ? 127 : rc;
#else
        execvp(argv[0], argv.data());
        // Only reached on failure.
        std::cerr << "icmg shield: execvp failed for '" << rest[0]
                  << "' (errno=" << errno << ")\n";
        return 127;
#endif
    }
};

ICMG_REGISTER_COMMAND("shield", ShieldCommand);

}  // namespace icmg::cli
