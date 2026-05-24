// v1.31.0 A8: `icmg git <args...>` thin wrapper.
//
// Reason: icmg-FIRST rule (CLAUDE.md routing) requires every native tool that
// has an icmg equivalent to go through icmg. Bare `git` calls leak through
// because the bash-rewrite PATTERN only catches read-ops (log/diff/show/status)
// — write-ops (commit/push/add/checkout) bypass the filter, encouraging
// assistant drift back to native git.
//
// This wrapper forwards `icmg git <args>` to the RunCommand pipeline by
// prepending "git" to argv and dispatching via Registry. Read-ops still get
// Tkil-filtered (60-90% noise drop); write-ops pass through with run_cmd's
// existing destructive-op gate (git push --force, reset --hard, clean -f).
//
// Net effect:
//   - single ergonomic entry point: `icmg git log`, `icmg git commit -m ...`
//   - same safety gates as `icmg run git ...` (no behavior split)
//   - matches the routing table (one column = one verb)
//
// Identity transform: `icmg git X Y Z` == `icmg run git X Y Z`.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include <iostream>
#include <vector>
#include <string>

namespace icmg::cli {

class GitCommand : public BaseCommand {
public:
    std::string name()        const override { return "git"; }
    std::string description() const override {
        return "Run git via icmg (Tkil-filtered for read-ops, safety-gated for write-ops)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg git <subcommand> [args...]\n\n"
            "Thin wrapper around `icmg run git ...`. Forces git through the icmg\n"
            "run pipeline so read-ops get Tkil-filtered and destructive ops hit\n"
            "the safety gate (git push --force / reset --hard / clean -f blocked).\n\n"
            "Examples:\n"
            "  icmg git status\n"
            "  icmg git log --oneline -20\n"
            "  icmg git diff HEAD~3\n"
            "  icmg git commit -m \"msg\"\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage();
            return args.empty() ? 1 : 0;
        }

        // Prepend "git" — RunCommand's argv is the command + its args.
        std::vector<std::string> forwarded;
        forwarded.reserve(args.size() + 1);
        forwarded.emplace_back("git");
        for (const auto& a : args) forwarded.push_back(a);

        auto& reg = core::Registry<BaseCommand>::instance();
        auto run_cmd = reg.create("run");
        if (!run_cmd) {
            std::cerr << "icmg git: internal — `run` command not registered\n";
            return 2;
        }
        return run_cmd->run(forwarded);
    }
};

ICMG_REGISTER_COMMAND("git", GitCommand);

} // namespace icmg::cli
