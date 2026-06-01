// Phase 35 T2: `icmg pr-summary` — generate PR description from git + verifications.
//
// Aggregates: commits since base, file-change stats, verifications status,
// known-issue matches, recent decisions. Outputs markdown ready for PR body.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#include "../../imem/memory_store.hpp"
#include <iostream>
#include <sstream>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <ctime>

namespace icmg::cli {

class PrSummaryCommand : public BaseCommand {
public:
    std::string name()        const override { return "pr-summary"; }
    std::string description() const override { return "Generate markdown PR description from git + verifications"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg pr-summary [options]\n\n"
            "Options:\n"
            "  --base BRANCH        Compare against (default: main)\n"
            "  --max-commits N      Cap commit list (default 50)\n"
            "  --include-decisions  Append recent decisions-* memory entries\n"
            "  --json               Machine output\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        std::string base       = flagValue(args, "--base", "main");
        bool include_decisions = hasFlag(args, "--include-decisions");
        bool json_out          = hasFlag(args, "--json");
        int max_commits = 50;
        try { max_commits = std::stoi(flagValue(args, "--max-commits", "50")); } catch (...) {}

        // 1. Commits.
        std::string log_cmd = "git log --oneline " + base + "..HEAD";
        auto log_res = core::safeExecShell(log_cmd, false, 10000);
        if (log_res.exit_code != 0) {
            std::cerr << "pr-summary: git log failed (exit=" << log_res.exit_code
                      << "). Is git repo? Is base '" << base << "' valid?\n";
            return 2;
        }
        std::vector<std::string> commits;
        std::istringstream is(log_res.out);
        std::string line;
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) commits.push_back(line);
            if ((int)commits.size() >= max_commits) break;
        }

        // 2. File-change stats.
        auto stat_res = core::safeExecShell("git diff --stat " + base + "..HEAD", false, 10000);

        // 3. Top-touched dirs.
        auto names_res = core::safeExecShell("git diff --name-only " + base + "..HEAD", false, 10000);
        std::map<std::string, int> dir_count;
        int total_files = 0;
        std::istringstream ns(names_res.out);
        while (std::getline(ns, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            ++total_files;
            auto slash = line.find('/');
            std::string dir = (slash == std::string::npos) ? "(root)" : line.substr(0, slash);
            ++dir_count[dir];
        }
        std::vector<std::pair<std::string,int>> dirs(dir_count.begin(), dir_count.end());
        std::sort(dirs.begin(), dirs.end(),
                  [](auto& a, auto& b){ return a.second > b.second; });

        // 4. Verifications (last 30d).
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        int v_pass = 0, v_fail = 0;
        std::vector<std::string> failing;
        try {
            int64_t cutoff = (int64_t)std::time(nullptr) - 30LL * 86400;
            db.query("SELECT command, exit_code FROM verifications "
                     "WHERE recorded_at > ? ORDER BY recorded_at DESC LIMIT 50",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){
                         if (r.size() < 2) return;
                         int rc = std::stoi(r[1]);
                         if (rc == 0) ++v_pass;
                         else { ++v_fail; if ((int)failing.size() < 5) failing.push_back(r[0]); }
                     });
        } catch (...) {}

        // 5. Recent decisions (optional).
        std::vector<std::string> decisions;
        if (include_decisions) {
            imem::MemoryStore mem(db);
            auto items = mem.recallByTopic("decisions-", 5);
            for (auto& n : items) decisions.push_back(n.topic + ": " + truncate(n.content, 120));
        }

        // Output.
        if (json_out) {
            std::cout << "{\"commits\":" << commits.size()
                      << ",\"files\":" << total_files
                      << ",\"verify_pass\":" << v_pass
                      << ",\"verify_fail\":" << v_fail
                      << "}\n";
            return 0;
        }

        std::cout << "## Summary\n";
        std::cout << "- " << commits.size() << " commit(s) since `" << base << "`\n";
        std::cout << "- " << total_files << " file(s) changed";
        if (!dirs.empty()) {
            std::cout << "; top areas:";
            int n = 0;
            for (auto& [d, c] : dirs) {
                if (n++ >= 5) break;
                std::cout << " `" << d << "` (" << c << ")";
            }
        }
        std::cout << "\n\n";

        std::cout << "## Commits\n";
        for (auto& c : commits) std::cout << "- " << c << "\n";
        if ((int)commits.size() == max_commits) std::cout << "- ... (capped at " << max_commits << ")\n";
        std::cout << "\n";

        if (!stat_res.out.empty()) {
            std::cout << "## Diff stat\n```\n";
            // Cap at 60 lines to avoid PR-body bloat.
            std::istringstream ss(stat_res.out);
            int n = 0;
            while (std::getline(ss, line) && n < 60) { std::cout << line << "\n"; ++n; }
            std::cout << "```\n\n";
        }

        std::cout << "## Verifications (last 30d)\n";
        if (v_pass == 0 && v_fail == 0) {
            std::cout << "(no verifications recorded — run `icmg verify --command \"<cmd>\"`)\n\n";
        } else {
            std::cout << "- ✅ " << v_pass << " pass\n";
            std::cout << "- ❌ " << v_fail << " fail\n";
            if (!failing.empty()) {
                std::cout << "\nRecent failures:\n";
                for (auto& f : failing) std::cout << "- `" << f << "`\n";
            }
            std::cout << "\n";
        }

        if (!decisions.empty()) {
            std::cout << "## Decisions captured\n";
            for (auto& d : decisions) std::cout << "- " << d << "\n";
            std::cout << "\n";
        }

        std::cout << "---\n_Generated by `icmg pr-summary`_\n";
        return 0;
    }

private:
    static std::string truncate(const std::string& s, size_t n) {
        return s.size() <= n ? s : s.substr(0, n) + "...";
    }
};

ICMG_REGISTER_COMMAND("pr-summary", PrSummaryCommand);

} // namespace icmg::cli
