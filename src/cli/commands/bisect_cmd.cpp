// v1.79 M5 smart-bisect: `icmg bisect` — auto git-bisect to first bad commit.
//   icmg bisect --good <ref> --bad <ref> --test "<cmd>" [--max N]
// Drives a binary search (bisect_logic::firstBadIndex) over the commits in
// good..bad, checking out each midpoint and running the test command. The
// first commit where the test fails (non-zero exit) is reported.
//
// SAFETY: refuses to run on a dirty working tree; saves + restores the original
// HEAD (branch or detached sha) on exit; never commits or rewrites history.
#include "../base_command.hpp"
#include "../bisect_logic.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

namespace icmg::cli {

namespace {

// Trim trailing CR/LF/space.
std::string rtrim(std::string s) {
    while (!s.empty() && (s.back()=='\n'||s.back()=='\r'||s.back()==' '||s.back()=='\t')) s.pop_back();
    return s;
}

// Run a git command, return trimmed stdout (merge_stderr=false).
std::string git(const std::string& args, int* exit_code = nullptr) {
    auto r = core::safeExecShell("git " + args, false, 30000);
    if (exit_code) *exit_code = r.exit_code;
    return rtrim(r.out);
}

} // namespace

class BisectCommand : public BaseCommand {
public:
    std::string name()        const override { return "bisect"; }
    std::string description() const override { return "Auto git-bisect to the first commit that fails a test"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg bisect --good <ref> --bad <ref> --test \"<cmd>\" [--max N]\n\n"
            "  --good <ref>   a known-GOOD commit (test passes)\n"
            "  --bad  <ref>   a known-BAD commit (test fails); default HEAD\n"
            "  --test \"<cmd>\" shell command; non-zero exit = BAD\n"
            "  --max  N       cap candidates (safety; default 200)\n\n"
            "Binary-searches good..bad, checking out each midpoint and running\n"
            "the test. Reports the first BAD commit. Refuses a dirty tree;\n"
            "restores your original HEAD when done.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h") || args.empty()) { usage(); return 0; }

        std::string good = flagValue(args, "--good", "");
        std::string bad  = flagValue(args, "--bad",  "HEAD");
        std::string test = flagValue(args, "--test", "");
        int maxc = 200;
        try { maxc = std::stoi(flagValue(args, "--max", "200")); } catch (...) {}

        if (good.empty() || test.empty()) {
            std::cerr << "icmg bisect: --good and --test are required.\n";
            return 1;
        }

        // Safety: refuse a dirty working tree.
        if (!git("status --porcelain").empty()) {
            std::cerr << "icmg bisect: working tree is dirty. Commit or stash first.\n";
            return 2;
        }

        // Save original HEAD (branch name, or sha if detached).
        int ec = 0;
        std::string orig = git("symbolic-ref --short -q HEAD", &ec);
        if (ec != 0 || orig.empty()) orig = git("rev-parse HEAD");
        if (orig.empty()) { std::cerr << "icmg bisect: cannot resolve HEAD.\n"; return 2; }

        // Ordered commit list oldest..newest: good (exclusive) .. bad (inclusive),
        // then prepend `good` as index 0 so commits[0] is the known-good anchor.
        std::string revs = git("rev-list --reverse " + good + ".." + bad, &ec);
        if (ec != 0) { std::cerr << "icmg bisect: git rev-list failed for " << good << ".." << bad << "\n"; return 2; }
        std::vector<std::string> commits;
        commits.push_back(git("rev-parse " + good));   // index 0 = good anchor
        { std::istringstream is(revs); std::string l; while (std::getline(is, l)) { l = rtrim(l); if (!l.empty()) commits.push_back(l); } }
        int n = (int)commits.size();
        if (n < 2) { std::cerr << "icmg bisect: no commits between good and bad.\n"; return 1; }
        if (n - 2 > maxc) { std::cerr << "icmg bisect: " << (n-2) << " candidates exceed --max " << maxc << ".\n"; return 1; }

        std::cout << "bisect: " << (n-1) << " commits, ~" << bisectStepsEstimate(n) << " test runs.\n";

        // Predicate: checkout commits[i], run test, non-zero exit = BAD. index 0
        // is the good anchor (assumed good, never tested -> false).
        auto isBad = [&](int i) -> bool {
            if (i == 0) return false;
            std::string sha = commits[i];
            git("checkout -q " + sha);
            auto r = core::safeExecShell(test, true, 600000);
            bool bad_here = (r.exit_code != 0);
            std::cout << "  " << sha.substr(0, 10) << "  " << (bad_here ? "BAD" : "good")
                      << "  (exit " << r.exit_code << ")\n";
            return bad_here;
        };

        int idx = firstBadIndex(n, isBad);

        // Restore original HEAD.
        git("checkout -q " + orig);

        if (idx <= 0) {
            std::cout << "bisect: no bad commit found in range (range may be all-good or mislabeled).\n";
            return 1;
        }
        std::string culprit = commits[idx];
        std::cout << "\nFirst BAD commit: " << culprit << "\n";
        std::cout << git("show --no-patch --format=%h%x20%s%x20(%an,%ar) " + culprit) << "\n";
        std::cout << "HEAD restored to " << orig << ".\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("bisect", BisectCommand);

} // namespace icmg::cli
