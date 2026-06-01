// Phase 31 T1: `icmg review` — PR pre-flight gate.
//
// Get changed files via `git diff --name-only <base>...HEAD`. For each, run:
//   - parity if matching template by file-stem
//   - lint-style if .icmg/style.json exists OR --ref provided
// Aggregate exit code = total warnings; --strict makes any warning fatal.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#include <iostream>
#include <sstream>
#include <filesystem>
#include <vector>
#include <string>
#ifdef _WIN32
  #include <windows.h>
#endif

namespace fs = std::filesystem;

namespace icmg::cli {

class ReviewCommand : public BaseCommand {
public:
    std::string name()        const override { return "review"; }
    std::string description() const override { return "PR pre-flight: parity + lint-style on git-diff changed files"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg review [options]\n\n"
            "Compares against base branch via `git diff --name-only`. Runs parity\n"
            "and lint-style on each changed file.\n\n"
            "Options:\n"
            "  --base BRANCH       Base branch (default: main)\n"
            "  --since REF         Use `git diff REF...HEAD` instead of base\n"
            "  --ref FILE          lint-style reference file (overrides per-file detect)\n"
            "  --rules JSON        lint-style custom rules JSON\n"
            "  --strict            Exit 1 on any warning\n"
            "  --json\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        std::string base    = flagValue(args, "--base", "main");
        std::string since   = flagValue(args, "--since");
        std::string ref     = flagValue(args, "--ref");
        std::string rules   = flagValue(args, "--rules");
        bool strict         = hasFlag(args, "--strict");
        bool json_out       = hasFlag(args, "--json");

        std::string diff_target = since.empty() ? (base + "...HEAD") : (since + "...HEAD");
        std::string git_cmd = "git diff --name-only " + diff_target;
        auto res = core::safeExecShell(git_cmd, false, 10000);
        if (res.exit_code != 0) {
            std::cerr << "icmg review: `git diff` failed (exit=" << res.exit_code
                      << "). Is git repo? Is base '" << base << "' valid?\n";
            return 2;
        }

        std::vector<std::string> changed;
        std::istringstream is(res.out);
        std::string line;
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) changed.push_back(line);
        }
        if (changed.empty()) {
            std::cout << "icmg review: no changes vs " << diff_target << "\n";
            return 0;
        }

        std::string self = locateSelf();
        struct Result { std::string file; std::string rule; int warnings; std::string head; };
        std::vector<Result> results;
        int total_warn = 0;

        // Look up matching templates for parity check.
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        for (auto& f : changed) {
            if (!fs::exists(f)) continue;   // deleted in this branch
            std::string stem = fs::path(f).stem().string();

            // Try template parity.
            std::string template_name;
            db.query("SELECT name FROM templates "
                     "WHERE source_path LIKE ? OR source_path LIKE ? OR name = ? LIMIT 1",
                     {"%/" + stem + ".%", "%\\" + stem + ".%", stem},
                     [&](const core::Row& r){ if (!r.empty()) template_name = r[0]; });
            if (!template_name.empty()) {
                std::string cmd = "\"" + self + "\" template apply " + template_name
                                + " --to \"" + f + "\" --check 2>&1";
                auto pr = core::safeExecShell(cmd, false, 30000);
                int n = pr.exit_code;
                results.push_back({f, "parity:" + template_name, n,
                                    headLine(pr.out, 140)});
                total_warn += n;
            }

            // lint-style: only if --ref or rules JSON provided OR .icmg/style.json present.
            bool has_rules = !rules.empty() || fs::exists(".icmg/style.json");
            if (!ref.empty() || has_rules) {
                std::string cmd = "\"" + self + "\" lint-style \"" + f + "\"";
                if (!ref.empty())   cmd += " --ref \"" + ref + "\"";
                if (!rules.empty()) cmd += " --rules \"" + rules + "\"";
                else if (fs::exists(".icmg/style.json"))
                    cmd += " --rules .icmg/style.json";
                cmd += " 2>&1";
                auto lr = core::safeExecShell(cmd, false, 20000);
                int n = lr.exit_code;
                if (n > 0) {
                    results.push_back({f, "lint-style", n, headLine(lr.out, 140)});
                    total_warn += n;
                }
            }
        }

        if (json_out) {
            std::cout << "{\"changed\":" << changed.size()
                      << ",\"warnings\":" << total_warn
                      << ",\"results\":[";
            for (size_t i = 0; i < results.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "{\"file\":\"" << esc(results[i].file)
                          << "\",\"rule\":\"" << esc(results[i].rule)
                          << "\",\"warnings\":" << results[i].warnings
                          << ",\"head\":\"" << esc(results[i].head) << "\"}";
            }
            std::cout << "]}\n";
        } else {
            std::cout << "icmg review: " << changed.size() << " changed file(s) vs " << diff_target << "\n";
            for (auto& r : results) {
                std::cout << "  " << r.file << "\n"
                          << "    [" << r.rule << "] " << r.warnings
                          << " warning(s): " << r.head << "\n";
            }
            std::cout << "\nSummary: " << total_warn << " warning(s) across "
                      << changed.size() << " files. Exit: " << total_warn << "\n";
        }

        if (strict && total_warn > 0) return 1;
        return total_warn;
    }

private:
    static std::string locateSelf() {
#ifdef _WIN32
        char buf[1024]; GetModuleFileNameA(nullptr, buf, sizeof(buf));
        return buf;
#else
        return "icmg";
#endif
    }
    static std::string headLine(const std::string& s, size_t n) {
        size_t nl = s.find('\n');
        std::string h = (nl == std::string::npos) ? s : s.substr(0, nl);
        if (h.size() > n) h = h.substr(0, n) + "...";
        return h;
    }
    static std::string esc(const std::string& s) {
        std::string out; out.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
            else if (c == '\n') out += "\\n";
            else out.push_back(c);
        }
        return out;
    }
};

ICMG_REGISTER_COMMAND("review", ReviewCommand);

} // namespace icmg::cli
