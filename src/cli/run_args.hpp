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
        if (!r.command.empty()) r.command += " ";
        r.command += runQuoteArg(a);
    }
    return r;
}

} // namespace icmg::cli
