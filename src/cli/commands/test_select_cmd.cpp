// Phase 65 T3: `icmg test-select` — diff-aware test selection.
//
// `git diff --name-only` → resolve modified files to test files via:
//   1. Direct: same-named test file (foo.cs → foo.test.cs / test_foo.py /
//      foo.spec.ts depending on lang convention)
//   2. Graph reverse-impact: who imports the modified file? Tests importing
//      it should re-run.
// Emits the run command (`pytest <files>` / `npx vitest run <files>` / etc.)
// or a list of test files for piping.
//
// Saves "run all tests" cycles where 1-3 tests would suffice.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#include "../../graph/graph_store.hpp"
#include <filesystem>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

class TestSelectCommand : public BaseCommand {
public:
    std::string name()        const override { return "test-select"; }
    std::string description() const override {
        return "Pick test files affected by current diff";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg test-select [options]\n\n"
            "Resolves `git diff --name-only` into the smallest set of test\n"
            "files that exercise modified code (direct + reverse-impact via\n"
            "graph). Avoids 'run all tests' on large repos.\n\n"
            "Options:\n"
            "  --ref REF      Compare against REF (default: working tree vs index)\n"
            "  --runner CMD   Wrap output in runner: pytest|vitest|jest|cargo|dotnet|none\n"
            "                 default: 'none' (just list files)\n"
            "  --json         Machine-readable output\n"
            "  --depth N      Reverse-impact graph depth (default 2)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        std::string ref    = flagValue(args, "--ref");
        std::string runner = flagValue(args, "--runner", "none");
        bool json_out      = hasFlag(args, "--json");
        int depth = 2;
        try { depth = std::stoi(flagValue(args, "--depth", "2")); } catch (...) {}

        // 1. git diff --name-only
        std::string cmd = "git diff --name-only";
        if (!ref.empty()) cmd += " " + ref;
        auto res = core::safeExec({"sh", "-c", cmd}, true, 30000);
        if (res.exit_code != 0) {
            std::cerr << "git diff failed: " << res.out << "\n";
            return 1;
        }

        std::vector<std::string> changed;
        std::istringstream iss(res.out);
        std::string line;
        while (std::getline(iss, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (!line.empty()) changed.push_back(line);
        }
        if (changed.empty()) {
            if (json_out) std::cout << "{\"selected\":[],\"reason\":\"no diff\"}\n";
            else          std::cout << "icmg test-select: no diff (clean working tree)\n";
            return 0;
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        std::set<std::string> tests;

        // 2a. Direct: changed file IS a test
        for (auto& f : changed) {
            if (isTestPath(f)) tests.insert(f);
        }

        // 2b. Sibling test (foo.cs → foo.test.cs / test_foo.py / foo.spec.ts)
        for (auto& f : changed) {
            if (isTestPath(f)) continue;
            for (auto& candidate : siblingTestCandidates(f)) {
                if (fs::exists(candidate)) tests.insert(candidate);
            }
        }

        // 2c. Reverse-impact via graph
        for (auto& f : changed) {
            auto upstreams = store.impact(f, depth);
            for (auto& u : upstreams) {
                if (isTestPath(u.path)) tests.insert(u.path);
            }
        }

        if (json_out) {
            std::cout << "{\"changed\":" << changed.size()
                      << ",\"selected\":[";
            size_t i = 0;
            for (auto& t : tests) {
                if (i++) std::cout << ",";
                std::cout << "\"" << t << "\"";
            }
            std::cout << "]}\n";
            return 0;
        }

        if (tests.empty()) {
            std::cout << "icmg test-select: " << changed.size()
                      << " file(s) changed, no test mapping found.\n"
                      << "  Hint: run `icmg graph scan` to refresh edges,\n"
                      << "  or add sibling tests (foo.cs → foo.test.cs).\n";
            return 0;
        }

        std::cout << "icmg test-select: " << tests.size()
                  << " test file(s) cover diff (" << changed.size()
                  << " changed):\n";
        for (auto& t : tests) std::cout << "  " << t << "\n";

        std::string runner_cmd = wrap(runner, tests);
        if (!runner_cmd.empty()) {
            std::cout << "\nRun:\n  " << runner_cmd << "\n";
        }
        return 0;
    }

private:
    static bool isTestPath(const std::string& p) {
        static const std::regex re(
            R"((^|/)(tests?|__tests__|spec)(/|$)|(\.test\.|\.spec\.|test_|_test\.))",
            std::regex::icase);
        return std::regex_search(p, re);
    }

    static std::vector<std::string> siblingTestCandidates(const std::string& src) {
        // foo/bar.cs → foo/bar.test.cs, foo/bar.spec.cs, foo/test_bar.cs, tests/test_bar.cs
        std::vector<std::string> out;
        fs::path p(src);
        std::string stem = p.stem().string();
        std::string ext  = p.extension().string();
        fs::path dir     = p.parent_path();
        for (auto* mid : {".test", ".spec"}) {
            out.push_back((dir / (stem + mid + ext)).string());
        }
        out.push_back((dir / ("test_" + stem + ext)).string());
        out.push_back((dir / (stem + "_test" + ext)).string());
        out.push_back(("tests/" + stem + ext));
        out.push_back(("tests/test_" + stem + ext));
        out.push_back(("test/" + stem + ext));
        return out;
    }

    static std::string wrap(const std::string& runner,
                             const std::set<std::string>& tests) {
        if (runner == "none" || runner.empty()) return "";
        std::ostringstream os;
        if      (runner == "pytest")  os << "pytest";
        else if (runner == "vitest")  os << "npx vitest run";
        else if (runner == "jest")    os << "npx jest";
        else if (runner == "cargo")   os << "cargo test --";
        else if (runner == "dotnet")  os << "dotnet test --filter";
        else                          os << runner;
        for (auto& t : tests) os << " " << t;
        return os.str();
    }
};

ICMG_REGISTER_COMMAND("test-select", TestSelectCommand);

} // namespace icmg::cli
