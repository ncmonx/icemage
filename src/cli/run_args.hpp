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
#include <cctype>

namespace icmg::cli {

struct RunArgs {
    bool raw = false, json_out = false, dry_run = false, stream = false,
         yes = false, ultra = false, no_delta = false, last_full = false, no_tier = false,
         nano = false, gist = false;
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
            else if (a == "--ultra")     { r.ultra     = true; continue; }
            else if (a == "--no-delta")   { r.no_delta  = true; continue; }
            else if (a == "--last-full")  { r.last_full = true; continue; }
            else if (a == "--no-tier")    { r.no_tier   = true; continue; }
            else if (a == "--nano")       { r.nano      = true; continue; }
            else if (a == "--gist")       { r.gist      = true; continue; }
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

// v2.20.0: argv-aware destructive detection (replaces the old whole-string
// substring scan, which false-positived on `grep 'rm -rf'`, a path like
// `src/farm/`, or any search pattern that merely CONTAINED "rm "/"-f"). We only
// flag when the LEADING verb of the command is the destructive tool, so an
// argument/quote/path that happens to contain the token no longer trips it.
//
// Notes:
//  - A single quoted token ("rm -rf /tmp/x") is split on whitespace first so
//    the leading verb is still recovered.
//  - Known wrapper prefixes (env VAR=val, sudo) are skipped to reach the verb.
namespace detail {
inline std::string lc(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
inline bool tokEq(const std::string& a, const char* b) { return lc(a) == b; }
// Flatten argv to whitespace tokens (splits a single quoted shell line too).
inline std::vector<std::string> flatten(const std::vector<std::string>& argv) {
    std::vector<std::string> out;
    for (const auto& a : argv) {
        std::string cur;
        for (char c : a) {
            if (c == ' ' || c == '\t') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
            else cur += c;
        }
        if (!cur.empty()) out.push_back(cur);
    }
    return out;
}
// Is this token an env-assignment (VAR=val) wrapper we should skip past?
inline bool isEnvAssign(const std::string& t) {
    auto eq = t.find('=');
    if (eq == std::string::npos || eq == 0) return false;
    for (size_t i = 0; i < eq; ++i) {
        char c = t[i];
        if (!(std::isalnum((unsigned char)c) || c == '_')) return false;
    }
    return true;
}
} // namespace detail

// True if a leading `ICMG_ASSUME_YES=1` (or =true/=yes) / `FORCE=1` env-prefix
// is present -- honored as explicit bypass intent even when the env didn't reach
// the icmg process itself (it was set for the wrapped child).
inline bool hasInlineYesPrefix(const std::vector<std::string>& argv) {
    for (const auto& raw : detail::flatten(argv)) {
        if (!detail::isEnvAssign(raw)) return false;   // stop at first non-env token
        auto eq = raw.find('=');
        std::string key = detail::lc(raw.substr(0, eq));
        std::string val = detail::lc(raw.substr(eq + 1));
        bool truthy = (val == "1" || val == "true" || val == "yes");
        if ((key == "icmg_assume_yes" || key == "force" || key == "icmg_yes") && truthy)
            return true;
    }
    return false;
}

inline bool isDestructiveArgv(const std::vector<std::string>& argv, std::string& reason) {
    using namespace detail;
    auto toks = flatten(argv);
    // Skip env-assignment + sudo wrappers to find the real leading verb.
    size_t i = 0;
    while (i < toks.size() && (isEnvAssign(toks[i]) || tokEq(toks[i], "sudo"))) ++i;
    if (i >= toks.size()) return false;
    const std::string verb = lc(toks[i]);
    auto rest = std::vector<std::string>(toks.begin() + i, toks.end());
    auto hasTok = [&](const char* f) {
        for (size_t k = i + 1; k < toks.size(); ++k) if (lc(toks[k]) == f) return true;
        return false;
    };
    // Any token starting with '-' that bundles r or f (rm -rf, rm -fr, rm -Rf...).
    auto hasRmForce = [&]() {
        for (size_t k = i + 1; k < toks.size(); ++k) {
            const std::string& t = toks[k];
            if (t.size() >= 2 && t[0] == '-' && t[1] != '-') {
                for (char c : t) if (c == 'r' || c == 'R' || c == 'f') return true;
            }
        }
        return false;
    };

    if (verb == "rm") {
        if (hasRmForce()) { reason = "rm with -r/-f"; return true; }
        return false;
    }
    if (verb == "remove-item") { reason = "Remove-Item"; return true; }
    if (verb == "rmdir") {
        if (hasTok("/s") || hasRmForce()) { reason = "rmdir /s"; return true; }
        return false;
    }
    if (verb == "git") {
        // git rm -r/-f, git push --force, git reset --hard, git clean -f
        if (toks.size() > i + 1) {
            const std::string sub = lc(toks[i + 1]);
            if (sub == "rm" && hasRmForce())        { reason = "git rm -r/-f";      return true; }
            if (sub == "push" && hasTok("--force")) { reason = "git push --force";  return true; }
            if (sub == "reset" && hasTok("--hard")) { reason = "git reset --hard";  return true; }
            if (sub == "clean" && hasRmForce())     { reason = "git clean -f";      return true; }
        }
        return false;
    }
    // SQL destructive statements can appear as an argument to a db CLI
    // (psql -c "DROP TABLE t"), so scan the joined REST for these phrases.
    std::string joined;
    for (auto& t : rest) { joined += lc(t); joined += ' '; }
    if (joined.find("delete from")  != std::string::npos) { reason = "DELETE FROM";   return true; }
    if (joined.find("drop table")   != std::string::npos) { reason = "DROP TABLE";    return true; }
    if (joined.find("drop database")!= std::string::npos) { reason = "DROP DATABASE"; return true; }
    if (joined.find("drop schema")  != std::string::npos) { reason = "DROP SCHEMA";   return true; }
    if (joined.find("truncate ")    != std::string::npos) { reason = "TRUNCATE";      return true; }
    return false;
}

} // namespace icmg::cli
