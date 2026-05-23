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

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"

#include <iostream>
#include <regex>
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
            "  -i, --ignore-case      Case-insensitive\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }

        // Build rg argv. Brace-expand any --glob value.
        std::vector<std::string> rg_argv;
        rg_argv.push_back("rg");
        for (size_t i = 0; i < args.size(); ++i) {
            const std::string& a = args[i];
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

        // Dispatch via `icmg run` path so Tkil filter + token cap apply.
        // Build a single shell command string.
        std::string cmd;
        for (auto& a : rg_argv) {
            if (!cmd.empty()) cmd += " ";
            // Quote args with spaces or braces.
            bool needs_q = a.find(' ') != std::string::npos
                        || a.find('{') != std::string::npos
                        || a.find('*') != std::string::npos;
            if (needs_q) cmd += "\"" + a + "\"";
            else         cmd += a;
        }
        // Forward to run_cmd via safeExecShell. Result.stdout printed.
        auto r = core::safeExecShell(cmd, false, 30000);
        if (!r.out.empty()) std::cout << r.out;
        if (!r.err.empty()) std::cerr << r.err;
        return r.exit_code;
    }
};

ICMG_REGISTER_COMMAND("grep", GrepCommand);

}  // namespace icmg::cli
