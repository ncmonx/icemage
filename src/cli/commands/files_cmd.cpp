// v1.29.0 #12: `icmg files [--glob <pat>] [--type <ext>] [<root>]` —
// recursive file enumeration with glob + brace expansion.
//
// Mirrors Claude Code Glob tool flag surface (`Glob('**/*.tsx')`) so AI
// doesn't drop to native Glob for file discovery — output goes through
// icmg's standard zone-aware filter + token cap.
//
// Examples:
//   icmg files --glob '**/*.{ts,tsx}'
//   icmg files --glob 'src/**/*.cpp' --type=cpp
//   icmg files src/cli/commands   (no glob = list all under root)

#include "../base_command.hpp"
#include "../../core/registry.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

// Recursive brace expansion: `{a,b}.{ts,tsx}` → 4 strings.
std::vector<std::string> braceExpand(const std::string& s) {
    std::vector<std::string> out;
    auto lb = s.find('{');
    if (lb == std::string::npos) { out.push_back(s); return out; }
    int d = 0; size_t rb = std::string::npos;
    for (size_t i = lb; i < s.size(); ++i) {
        if (s[i] == '{') ++d;
        else if (s[i] == '}') { if (--d == 0) { rb = i; break; } }
    }
    if (rb == std::string::npos) { out.push_back(s); return out; }
    std::string prefix = s.substr(0, lb), suffix = s.substr(rb + 1);
    std::string body   = s.substr(lb + 1, rb - lb - 1);
    std::vector<std::string> alts; std::string cur; int dd = 0;
    for (char c : body) {
        if (c == '{') ++dd;
        else if (c == '}') --dd;
        if (c == ',' && dd == 0) { alts.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    alts.push_back(cur);
    for (auto& a : alts)
        for (auto& e : braceExpand(prefix + a + suffix))
            out.push_back(e);
    return out;
}

// Convert glob (`**`, `*`, `?`) to regex.
std::string globToRegex(const std::string& g) {
    std::string r; r.reserve(g.size() * 2 + 4);
    r.push_back('^');
    for (size_t i = 0; i < g.size(); ++i) {
        char c = g[i];
        if (c == '*') {
            if (i + 1 < g.size() && g[i + 1] == '*') { r += ".*"; ++i; }
            else r += "[^/\\\\]*";
        } else if (c == '?') {
            r += "[^/\\\\]";
        } else if (c == '.' || c == '+' || c == '(' || c == ')' || c == '|'
                || c == '^' || c == '$' || c == '[' || c == ']' || c == '\\') {
            r.push_back('\\'); r.push_back(c);
        } else {
            r.push_back(c);
        }
    }
    r.push_back('$');
    return r;
}

}  // namespace

class FilesCommand : public BaseCommand {
public:
    std::string name()        const override { return "files"; }
    std::string description() const override {
        return "Recursive file enumeration with glob (mirrors Claude Glob tool)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg files [<root>] [options]\n\n"
            "Options:\n"
            "  --glob '<pat>'    Glob filter. Brace expansion {a,b,c} supported.\n"
            "                    `**` matches across dirs; `*` within filename.\n"
            "  --type <ext>      Filter by extension (cpp, ts, py, ...). Repeatable.\n"
            "  --max <N>         Cap output at N matches (default 500)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        std::string glob = flagValue(args, "--glob");
        int max_n = 500;
        try { max_n = std::stoi(flagValue(args, "--max", "500")); } catch (...) {}

        std::vector<std::string> ext_filters;
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == "--type") ext_filters.push_back("." + args[i + 1]);
        }

        // v1.29.0 fix: skip the VALUE arg after --glob/--type/--max so the
        // glob pattern isn't picked up as the positional root.
        std::string root_arg;
        for (size_t i = 0; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (a == "--glob" || a == "--type" || a == "--max") {
                ++i;  // skip value
                continue;
            }
            if (!a.empty() && a[0] == '-') continue;
            root_arg = a;
            break;
        }
        fs::path root = root_arg.empty() ? fs::current_path() : fs::path(root_arg);

        std::vector<std::regex> patterns;
        if (!glob.empty()) {
            for (auto& g : braceExpand(glob)) {
                try { patterns.emplace_back(globToRegex(g), std::regex::ECMAScript); }
                catch (...) {}
            }
        }

        int shown = 0;
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            std::cerr << "icmg files: cannot iterate root: " << root.string()
                      << " (" << ec.message() << ")\n";
            return 1;
        }
        const fs::recursive_directory_iterator end_it;
        for (; it != end_it; ++it) {
            std::error_code lec;
            auto fname = it->path().filename().string();
            // Skip noise dirs (and stop descending into them).
            if (it->is_directory(lec)
                && (fname == ".git" || fname == "node_modules" || fname == "build"
                 || fname == "build-linux" || fname == ".icmg"
                 || fname == "third_party" || fname == "dist")) {
                it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(lec)) continue;
            if (!ext_filters.empty()) {
                std::string ext = it->path().extension().string();
                if (std::find(ext_filters.begin(), ext_filters.end(), ext) == ext_filters.end())
                    continue;
            }
            std::string rel = fs::relative(it->path(), root, ec).generic_string();
            if (!patterns.empty()) {
                bool m = false;
                for (auto& re : patterns) {
                    if (std::regex_match(rel, re)) { m = true; break; }
                }
                if (!m) continue;
            }
            std::cout << rel << "\n";
            if (++shown >= max_n) {
                std::cerr << "icmg files: capped at " << max_n
                          << " (pass --max=N for more)\n";
                break;
            }
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("files", FilesCommand);

}  // namespace icmg::cli
