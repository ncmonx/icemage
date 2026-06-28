// Pure decision for what to do with a "noisy" bash command that already passed
// the cheap bash-hook PATTERN gate. Extracted so the pipe/exempt/quote logic is
// unit-tested in one place instead of living only as untestable bash regex.
//
// Born from the 2026-06-28 search-escape audit: 82% of RAW=1 search bypasses
// were grep used as a PIPE filter (`ls | grep`, `curl | grep`, `strings | grep`).
// Those are not codebase search -- the model is already self-filtering another
// command's stdout. Two consequences this header encodes:
//   1. A pipe ENDING in a self-filter (grep/head/tail/wc/...) => EXEMPT (the model
//      already reduced output; wrapping in `icmg run` adds nothing). Kills the nag.
//   2. A pipe NOT ending in a self-filter => redirect with the CORRECT quoted form
//      `icmg run "<full pipe>"` -- because the old advice `icmg run $CMD` (unquoted)
//      let the outer shell split the pipe and leak the tail stage back to native.
//   3. No pipe => per-command advice (sed->fuzzy-edit, python->calc, tail/head->context).
#ifndef ICMG_BASH_REDIRECT_HPP
#define ICMG_BASH_REDIRECT_HPP

#include <algorithm>
#include <cctype>
#include <set>
#include <string>

namespace icmg::cli {

enum class RedirectKind {
    Exempt,          // let it run native -- no nag
    RedirectPlain,   // single noisy command -> per-command icmg advice
    RedirectQuoted   // pipe -> wrap the WHOLE thing: icmg run "<cmd>"
};

struct RedirectDecision {
    RedirectKind kind = RedirectKind::RedirectPlain;
    std::string  message;   // deny text; empty when kind == Exempt
};

namespace redir_detail {

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline std::string firstToken(const std::string& s) {
    std::string t = trim(s);
    size_t i = 0;
    while (i < t.size() && !std::isspace((unsigned char)t[i])) ++i;
    return t.substr(0, i);
}

// A genuine output-reducer: if a pipe ends in one of these, the model already
// filtered the stream. (PowerShell filters included for the PS hook path.)
inline bool isSelfFilter(const std::string& tok) {
    static const std::set<std::string> f = {
        "grep", "egrep", "fgrep", "rg", "ag",
        "head", "tail", "wc", "sort", "uniq", "cut",
        "Select-String", "findstr",
        "Measure-Object", "Sort-Object", "Where-Object", "Select-Object"
    };
    return f.count(tok) > 0;
}

// Return the command segment after the LAST top-level pipe `|` (ignoring the
// logical-or `||`). If there is no real pipe, returns empty.
inline std::string lastPipeStage(const std::string& cmd) {
    int last = -1;
    for (size_t i = 0; i < cmd.size(); ++i) {
        if (cmd[i] == '|') {
            bool doubled = (i + 1 < cmd.size() && cmd[i + 1] == '|') ||
                           (i > 0 && cmd[i - 1] == '|');
            if (!doubled) last = (int)i;
            if (i + 1 < cmd.size() && cmd[i + 1] == '|') ++i;  // skip second '|'
        }
    }
    if (last < 0) return "";
    return cmd.substr((size_t)last + 1);
}

inline bool hasRealPipe(const std::string& cmd) {
    return !lastPipeStage(cmd).empty() ||
           // a trailing bare '|' is still a pipe intent
           (cmd.find('|') != std::string::npos && cmd.find("||") == std::string::npos);
}

}  // namespace redir_detail

// Classify a noisy command (already matched the bash PATTERN gate). `cmd` is the
// full command string (wrappers like `bash -c` already stripped by the caller).
inline RedirectDecision classifyBashRedirect(const std::string& cmd_in) {
    using namespace redir_detail;
    RedirectDecision d;
    std::string cmd = trim(cmd_in);

    // --- pipe path ---
    std::string tail = lastPipeStage(cmd);
    if (!tail.empty()) {
        std::string tailTok = firstToken(tail);
        if (isSelfFilter(tailTok)) {
            d.kind = RedirectKind::Exempt;   // model already self-filtering
            return d;
        }
        d.kind = RedirectKind::RedirectQuoted;
        d.message = "This is a pipe. Wrap the WHOLE pipeline so icmg filters it: "
                    "`icmg run \"" + cmd + "\"` (the quotes matter -- unquoted, the "
                    "outer shell splits the pipe and the tail stage leaks to native). "
                    "Bypass: RAW=1.";
        return d;
    }

    // --- single-command path: per-command advice ---
    std::string tok = firstToken(cmd);
    d.kind = RedirectKind::RedirectPlain;
    if (tok == "sed") {
        d.message = "For in-place edits use `icmg fuzzy-edit <file> --old <text> --new <text>` "
                    "(tolerant of indent/CRLF drift). For stream filtering wrap with "
                    "`icmg run " + cmd + "`. Bypass: RAW=1.";
    } else if (tok == "python" || tok == "python3" || tok == "py" ||
               tok == "node" || tok == "deno" || tok == "bun") {
        d.message = "If this is a quick calculation use `icmg calc \"<expr>\"` (offline, no "
                    "interpreter). Otherwise wrap with `icmg run " + cmd + "` for filtered "
                    "output. Bypass: RAW=1.";
    } else if (tok == "tail" || tok == "head") {
        d.message = "For first/last N lines of a file use `icmg context <file> --head N` / "
                    "`--tail N` (line-numbered, memory-aware). Bypass: RAW=1.";
    } else {
        d.message = "Use `icmg run " + cmd + "` for token-filtered output (60-90% smaller). "
                    "Bypass with RAW=1 prefix.";
    }
    return d;
}

}  // namespace icmg::cli

#endif  // ICMG_BASH_REDIRECT_HPP
