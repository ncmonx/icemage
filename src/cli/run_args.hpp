#pragma once
// v1.70.0 (#178): `icmg run` argument parsing — extracted for testability.
//
// Bug fixed: icmg run interpreted ANY --flag anywhere in argv as its own, so a
// child flag (e.g. `./tool --json`) was eaten and never reached the child, and
// a bare `--` end-of-options marker was passed literally to bash ("--: command
// not found"). parseRunArgs now interprets icmg-run flags ONLY in the leading
// flag region; the first non-flag token starts the command, and everything
// after it — or after a bare `--` — is passed verbatim to the child.
#include <string>
#include <vector>

namespace icmg::cli {

struct RunArgs {
    bool raw = false, json_out = false, dry_run = false, stream = false,
         yes = false, ultra = false;
    std::string              command;    // quoted, shell-ready child command line
    std::vector<std::string> cmd_args;   // raw (unquoted) child tokens
};

// Quote a token containing whitespace/quotes so a downstream argv parser can
// recover the original token (paths with spaces must not fragment).
inline std::string runQuoteArg(const std::string& a) {
    if (a.empty()) return "\"\"";
    if (a.find_first_of(" \t\"") == std::string::npos) return a;
    std::string out = "\"";
    for (char c : a) {
        if (c == '"') out += "\\\"";
        else          out += c;
    }
    out += "\"";
    return out;
}

inline RunArgs parseRunArgs(const std::vector<std::string>& args) {
    RunArgs r;
    bool in_cmd = false;       // first command token seen -> stop parsing our flags
    bool passthrough = false;  // bare "--" seen -> everything after is verbatim
    for (const auto& a : args) {
        if (a.empty()) continue;
        if (!in_cmd && !passthrough && a == "--") { passthrough = true; continue; }
        if (!in_cmd && !passthrough && a.size() > 1 && a[0] == '-') {
            if      (a == "--raw")     { r.raw = true;      continue; }
            else if (a == "--json")    { r.json_out = true; continue; }
            else if (a == "--dry-run") { r.dry_run = true;  continue; }
            else if (a == "--stream")  { r.stream = true;   continue; }
            else if (a == "--yes" || a == "-y") { r.yes = true; continue; }
            else if (a == "--ultra")   { r.ultra = true;    continue; }
            // unknown leading flag -> treat as the start of the child command
        }
        in_cmd = true;
        r.cmd_args.push_back(a);
    }
    // Build the shell-ready command line. A SINGLE token is already a complete
    // shell line (the user quoted the whole command: `icmg run "ls | grep x"`),
    // so use it verbatim -- re-quoting it would make `bash -c` treat the entire
    // line as one command name ("ls | grep x: command not found"). Multiple
    // tokens are joined with per-token quoting so spaces survive as word
    // boundaries for the downstream shell.
    if (r.cmd_args.size() == 1) {
        r.command = r.cmd_args[0];
    } else {
        for (const auto& a : r.cmd_args) {
            if (!r.command.empty()) r.command += " ";
            r.command += runQuoteArg(a);
        }
    }
    return r;
}


// v1.74.0 (#184): decide what to do with a destructive command. Pure +
// testable. In a non-interactive context (stdin not a TTY) we must NOT block
// on a confirmation prompt — that hangs scripts/agents forever. Auto-deny
// instead; the user can opt in via --yes / ICMG_ASSUME_YES=1.
enum class DestructiveDecision { Proceed, Deny, Prompt };

inline DestructiveDecision destructiveDecision(bool yes_flag, bool assume_yes_env,
                                               bool is_destructive, bool targets_safe,
                                               bool stdin_is_tty) {
    if (!is_destructive || targets_safe) return DestructiveDecision::Proceed;
    if (yes_flag || assume_yes_env)      return DestructiveDecision::Proceed;
    if (!stdin_is_tty)                   return DestructiveDecision::Deny;   // no hang
    return DestructiveDecision::Prompt;
}

} // namespace icmg::cli
