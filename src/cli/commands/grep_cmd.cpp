// v1.29.0 #4 + #11: `icmg grep` — thin rg wrapper that mirrors Claude
// Code's Grep tool flag surface so AI doesn't need to remember rg's
// full flag set.
//
// Maps:
//   --glob 'pattern'       → rg --glob
//   --type cpp             → rg --type=cpp
//   -A N / -B N / -C N     → rg -A/-B/-C
//   -n / --line-number     → rg -n
//   -i / --ignore-case     → rg -i
//
// Brace expansion (#4): `--glob 'menus/{a,b,c}.vue'` expands to 3 separate
// --glob args (recursive, supports nested `{x,{y,z}}`).
//
// Token filter (#4): identical to `icmg run rg ...` — auto-detects rg
// vs grep, applies Tkil filtering on output, capped at 8KB.
//
// --smart (v2.8.1): auto-expands the pattern to all case-convention variants
// so a single search finds logQuery, log_query, LogQuery, log-query etc.
// Eliminates the "grep N times for 1 symbol" anti-pattern.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../graph/graph_store.hpp"
#include "../grep_symbols.hpp"

#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

namespace {

// Recursive brace expansion: `a{1,2}/{x,y}.cpp` → 4 strings.
std::vector<std::string> braceExpand(const std::string& s) {
    std::vector<std::string> out;
    auto lb = s.find('{');
    if (lb == std::string::npos) { out.push_back(s); return out; }
    int depth = 0;
    size_t rb = std::string::npos;
    for (size_t i = lb; i < s.size(); ++i) {
        if (s[i] == '{') ++depth;
        else if (s[i] == '}') {
            if (--depth == 0) { rb = i; break; }
        }
    }
    if (rb == std::string::npos) { out.push_back(s); return out; }
    std::string prefix = s.substr(0, lb);
    std::string suffix = s.substr(rb + 1);
    std::string body   = s.substr(lb + 1, rb - lb - 1);
    // Split body by top-level commas.
    std::vector<std::string> alts;
    {
        int d = 0; std::string cur;
        for (char c : body) {
            if (c == '{') ++d;
            else if (c == '}') --d;
            if (c == ',' && d == 0) { alts.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        alts.push_back(cur);
    }
    for (auto& a : alts) {
        for (auto& exp : braceExpand(prefix + a + suffix)) {
            out.push_back(exp);
        }
    }
    return out;
}

// --smart: expand a symbol name into all common case-convention variants
// so the agent finds logQuery, log_query, LogQuery, LOG_QUERY, log-query
// in one pass instead of grep-retry loops.
//
// Strategy:
//   1. Tokenise input by camelCase boundaries + existing _ / - separators.
//   2. Emit: camelCase, PascalCase, snake_case, SCREAMING_SNAKE, kebab-case.
//   3. Deduplicate and join as a rg alternation pattern: (a|b|c|d|e).
std::string smartExpand(const std::string& pat) {
    // Skip patterns that already look like regex (contain ^$[].*+?|)
    for (char c : pat) {
        if (c == '^' || c == '$' || c == '[' || c == ']' ||
            c == '.' || c == '*' || c == '+' || c == '?' || c == '|')
            return pat;
    }

    // Tokenise: split on _ / - and camelCase boundaries.
    std::vector<std::string> tokens;
    std::string cur;
    for (size_t i = 0; i < pat.size(); ++i) {
        char c = pat[i];
        if (c == '_' || c == '-') {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            continue;
        }
        // camelCase split: uppercase after lowercase, or uppercase before lowercase
        // when preceded by uppercase (e.g. HTMLParser → HTML + Parser).
        if (!cur.empty() && std::isupper((unsigned char)c)) {
            bool prev_lower = std::islower((unsigned char)cur.back());
            bool next_lower = (i + 1 < pat.size()) && std::islower((unsigned char)pat[i+1]);
            if (prev_lower || (next_lower && std::isupper((unsigned char)cur.back()))) {
                tokens.push_back(cur); cur.clear();
            }
        }
        cur.push_back(c);
    }
    if (!cur.empty()) tokens.push_back(cur);

    if (tokens.size() <= 1) return pat;  // single token, nothing to expand

    // Lower-case all tokens for building variants.
    std::vector<std::string> low;
    for (auto& t : tokens) {
        std::string l; for (char c : t) l.push_back((char)std::tolower((unsigned char)c));
        low.push_back(l);
    }
    auto capitalize = [](const std::string& s) -> std::string {
        if (s.empty()) return s;
        std::string r = s;
        r[0] = (char)std::toupper((unsigned char)r[0]);
        return r;
    };
    auto upper = [](const std::string& s) -> std::string {
        std::string r; for (char c : s) r.push_back((char)std::toupper((unsigned char)c));
        return r;
    };

    // camelCase: first token lower, rest capitalized
    std::string camel = low[0];
    for (size_t i = 1; i < low.size(); ++i) camel += capitalize(low[i]);

    // PascalCase
    std::string pascal;
    for (auto& t : low) pascal += capitalize(t);

    // snake_case
    std::string snake = low[0];
    for (size_t i = 1; i < low.size(); ++i) snake += "_" + low[i];

    // SCREAMING_SNAKE
    std::string screaming = upper(low[0]);
    for (size_t i = 1; i < low.size(); ++i) screaming += "_" + upper(low[i]);

    // kebab-case
    std::string kebab = low[0];
    for (size_t i = 1; i < low.size(); ++i) kebab += "-" + low[i];

    // Deduplicate while preserving order.
    std::vector<std::string> variants;
    std::set<std::string> seen;
    for (auto& v : {camel, pascal, snake, screaming, kebab, pat}) {
        if (seen.insert(v).second) variants.push_back(v);
    }

    // Build rg alternation without outer parens: a|b|c
    // rg supports top-level alternation without grouping parens, and
    // no-paren form avoids bash subshell interpretation in -lc routing.
    std::string result;
    for (size_t i = 0; i < variants.size(); ++i) {
        if (i) result += "|";
        result += variants[i];
    }
    return result;
}

}  // namespace

class GrepCommand : public BaseCommand {
public:
    std::string name()        const override { return "grep"; }
    std::string description() const override {
        return "Thin rg wrapper with brace-expanded --glob (mirrors Claude Grep tool flags)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg grep <pattern> [options] [path...]\n\n"
            "Options (rg passthrough):\n"
            "  --glob '<pattern>'     Filter files. Supports brace expansion {a,b,c}\n"
            "  --type <lang>          rg --type filter (cpp, py, js, ts, ...)\n"
            "  -A <N>                 N lines after match\n"
            "  -B <N>                 N lines before match\n"
            "  -C <N>                 N lines around (alias for -A=-B=N)\n"
            "  -n, --line-number      Show line numbers\n"
            "  -i, --ignore-case      Case-insensitive\n"
            "  --symbols              Group matches under their enclosing function/class (graph)\n"
            "  --smart                Auto-expand pattern to all case variants in one pass\n"
            "                         logQuery -> (logQuery|LogQuery|log_query|LOG_QUERY|log-query)\n"
            "                         Eliminates grep-retry loops for renamed/convention-varied symbols\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }

        bool symbols_mode = hasFlag(args, "--symbols");
        bool smart_mode   = hasFlag(args, "--smart");

        // Build rg argv. Brace-expand any --glob value.
        std::vector<std::string> rg_argv;
        rg_argv.push_back("rg");
        bool has_n = false;
        for (size_t i = 0; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (a == "--symbols") continue;   // icmg-only flag, not for rg
            if (a == "--smart")   continue;   // icmg-only flag, not for rg
            if (a == "-n" || a == "--line-number") has_n = true;
            if (a == "--glob" && i + 1 < args.size()) {
                auto patterns = braceExpand(args[++i]);
                for (auto& p : patterns) {
                    rg_argv.push_back("--glob");
                    rg_argv.push_back(p);
                }
                continue;
            }
            rg_argv.push_back(a);
        }
        // --symbols needs line numbers to resolve the enclosing symbol, and
        // --with-filename so a single explicit file still prefixes path: (rg
        // drops the path when given exactly one file, which breaks parsing).
        if (symbols_mode) {
            if (!has_n) rg_argv.push_back("-n");
            rg_argv.push_back("--with-filename");
        }

        // --smart: replace pattern (first non-flag rg arg) with expanded alternation.
        // Uses "-e <pattern>" instead of a positional arg so cmd.exe on Windows
        // does not interpret the alternation parens as grouping syntax.
        if (smart_mode && rg_argv.size() >= 2) {
            for (size_t i = 1; i < rg_argv.size(); ++i) {
                if (rg_argv[i].empty() || rg_argv[i][0] == '-') continue;
                std::string expanded = smartExpand(rg_argv[i]);
                if (expanded != rg_argv[i]) {
                    std::cerr << "[smart] " << rg_argv[i]
                              << " -> " << expanded << "\n";
                    // Replace positional pattern with explicit -e flag so the
                    // shell does not misparse the alternation parens.
                    rg_argv.erase(rg_argv.begin() + (int)i);
                    rg_argv.insert(rg_argv.begin() + (int)i, expanded);
                    rg_argv.insert(rg_argv.begin() + (int)i, "-e");
                }
                break;
            }
        }

        // Dispatch: --smart uses direct argv (safeExec) to bypass cmd.exe
        // quote-stripping which breaks alternation parens on Windows.
        // Normal mode uses safeExecShell so Tkil filter + token cap apply.
        core::ExecResult r;
        if (smart_mode) {
            // Direct argv exec â€” no shell interpolation, no cmd.exe mangling.
            r = core::safeExec(rg_argv, false, 30000);
        } else {
            std::string cmd;
            for (auto& a : rg_argv) {
                if (!cmd.empty()) cmd += " ";
                bool needs_q = a.find(' ') != std::string::npos
                            || a.find('{') != std::string::npos
                            || a.find('*') != std::string::npos;
                if (needs_q) cmd += "\"" + a + "\"";
                else         cmd += a;
            }
            r = core::safeExecShell(cmd, false, 30000);
        }

        if (symbols_mode) {
            // Parse rg rows, resolve each match's enclosing symbol from the
            // graph, then render grouped-by-symbol. Falls back to raw output
            // when nothing parses (e.g. rg printed an error).
            auto matches = parseGrepMatches(r.out);
            if (!matches.empty()) {
                try {
                    auto& cfg = core::Config::instance();
                    core::Db db(cfg.projectDbPath("."));
                    graph::GraphStore store(db);
                    // Cache file-node -> child symbols so repeated hits in the
                    // same file don't re-query the graph.
                    std::map<std::string, std::vector<graph::GraphNode>> sym_cache;
                    for (auto& m : matches) {
                        auto it = sym_cache.find(m.path);
                        if (it == sym_cache.end()) {
                            std::vector<graph::GraphNode> kids;
                            // getNode resolves relative/abs/slash path variants.
                            if (auto fn = store.getNode(m.path); fn && fn->id > 0)
                                kids = store.childrenOf(fn->id);
                            it = sym_cache.emplace(m.path, std::move(kids)).first;
                        }
                        // Tightest enclosing symbol containing this line.
                        const graph::GraphNode* best = nullptr;
                        for (const auto& k : it->second) {
                            if (k.kind == "file") continue;
                            if (k.line_start <= m.line && m.line <= k.line_end) {
                                if (!best ||
                                    (k.line_end - k.line_start) < (best->line_end - best->line_start))
                                    best = &k;
                            }
                        }
                        if (best) { m.symbol = best->symbol_name; m.kind = best->kind; }
                    }
                } catch (...) { /* no graph DB — render with empty symbols */ }
                std::cout << renderSymbolGrep(matches);
                if (!r.err.empty()) std::cerr << r.err;
                return r.exit_code;
            }
            // else: fall through to raw output below.
        }

        if (!r.out.empty()) std::cout << r.out;
        if (!r.err.empty()) std::cerr << r.err;
        return r.exit_code;
    }
};

ICMG_REGISTER_COMMAND("grep", GrepCommand);

}  // namespace icmg::cli
