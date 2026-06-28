// Feature D (2026-06-15): batch read-many for `icmg context`.
//
// `icmg context a.hpp b.cpp c.ts` should pull a bundle for EACH file in one
// call (token-efficient multi-read) instead of silently showing only the
// first. The tricky part is telling FILE args apart from the VALUE of a
// value-taking flag (e.g. `--lines 5-10` -> "5-10" is not a file).
//
// Two pure helpers live here so the arg-splitting is unit-testable without a
// DB or filesystem; the command loops over collectContextFiles() and rebuilds
// per-file args via singleFileArgs().
//
// Header-only so tests can include without linking icmg_lib.

#pragma once
#include <set>
#include <string>
#include <vector>

namespace icmg::cli {

// Flags that consume the following token as their value (in `--key value`
// form). The `--key=value` form is self-contained and needs no skip.
inline const std::set<std::string>& contextValueFlags() {
    static const std::set<std::string> v = {
        "--max-bytes", "--lines", "--for", "--symbol",
        "--head", "--tail"   // v2.11.2: consume their N value (not a file token)
    };
    return v;
}

// Return the file arguments: non-flag tokens that are NOT the value of a
// preceding value-taking flag. Preserves order; duplicates kept (caller may
// dedup if desired).
inline std::vector<std::string> collectContextFiles(
        const std::vector<std::string>& args,
        const std::set<std::string>& value_flags = contextValueFlags()) {
    std::vector<std::string> files;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (!a.empty() && a[0] == '-') {
            // value-flag in "--key value" form consumes the next token
            if (value_flags.count(a) && a.find('=') == std::string::npos
                && i + 1 < args.size())
                ++i;
            continue;
        }
        files.push_back(a);
    }
    return files;
}

// Rebuild an args vector for a SINGLE file: keep all flags (and their values)
// but drop every file token except `keep`. Used to dispatch the existing
// single-file path per file in batch mode.
inline std::vector<std::string> singleFileArgs(
        const std::vector<std::string>& args,
        const std::string& keep,
        const std::set<std::string>& value_flags = contextValueFlags()) {
    std::vector<std::string> sub;
    bool kept = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (!a.empty() && a[0] == '-') {
            sub.push_back(a);
            if (value_flags.count(a) && a.find('=') == std::string::npos
                && i + 1 < args.size())
                sub.push_back(args[++i]);
            continue;
        }
        // file token: keep only the first occurrence matching `keep`
        if (!kept && a == keep) { sub.push_back(a); kept = true; }
    }
    return sub;
}

// Parse `git diff --name-only` output into a file list: one path per line,
// trimmed of CR/LF, blanks skipped. Order preserved, duplicates dropped
// (a file can appear once for staged + once for unstaged in some diff modes).
inline std::vector<std::string> parseChangedFiles(const std::string& git_output) {
    std::vector<std::string> files;
    std::set<std::string> seen;
    std::string ln;
    for (size_t i = 0; i <= git_output.size(); ++i) {
        if (i == git_output.size() || git_output[i] == '\n') {
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' '))
                ln.pop_back();
            if (!ln.empty() && seen.insert(ln).second) files.push_back(ln);
            ln.clear();
        } else {
            ln.push_back(git_output[i]);
        }
    }
    return files;
}

// Feature E (2026-06-15): map a context selector flag to the git command that
// lists its files. "--changed" = working-tree vs HEAD; "--staged" = index vs
// HEAD (pre-commit review). Empty string for an unknown flag.
inline std::string gitListCmdForFlag(const std::string& flag) {
    if (flag == "--changed") return "git diff --name-only HEAD";
    if (flag == "--staged")  return "git diff --cached --name-only";
    return "";
}

} // namespace icmg::cli
