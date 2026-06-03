// Phase 21 Task 6: streaming filter middleware.
//
// Reads stdin → applies a Tkil filter (auto-detect by --type or --as command
// hint) → writes filtered output to stdout. Useful for pipes:
//
//   npm test 2>&1 | icmg filter test
//   git diff           | icmg filter --as "git diff"
//   sqlcmd -Q "..."    | icmg filter db
//
// Buffers full input then runs filter once. True line-streaming would require
// per-filter chunk-aware logic; deferred.

#include "../base_command.hpp"
#include "../../core/stdin_util.hpp"
#include "../../core/registry.hpp"
#include "../../tkil/base_filter.hpp"
#include "../../tkil/detector.hpp"
#include <iostream>
#include <sstream>
#include <string>

namespace icmg::cli {

class FilterCommand : public BaseCommand {
public:
    std::string name()        const override { return "filter"; }
    std::string description() const override { return "Apply Tkil filter to stdin (pipe-style)"; }

    void usage() const override {
        std::cout <<
            "Usage:\n"
            "  <cmd> | icmg filter <type>\n"
            "  <cmd> | icmg filter --as \"<original-cmd-string>\"\n\n"
            "Types: git | build | test | search | npm | db | default\n\n"
            "--as detects type by command-string hint (uses Tkil detector).\n"
            "Examples:\n"
            "  npm test 2>&1 | icmg filter test\n"
            "  git diff       | icmg filter --as \"git diff\"\n"
            "  sqlcmd -Q ...  | icmg filter db\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage(); return 0;
        }

        // Resolve filter key
        std::string key;
        std::string command_hint;
        std::string as_hint = flagValue(args, "--as");
        if (!as_hint.empty()) {
            // Use detector to map command → filter
            tkil::Detector det;
            tkil::CmdType type = det.detect(as_hint);
            switch (type) {
                case tkil::CmdType::GitLog:         key = "git";     break;
                case tkil::CmdType::Build:          key = "build";   break;
                case tkil::CmdType::Test:           key = "test";    break;
                case tkil::CmdType::Search:         key = "search";  break;
                case tkil::CmdType::Docker:         key = "build";   break;
                case tkil::CmdType::PackageManager: key = "npm";     break;
                case tkil::CmdType::Db:             key = "db";      break;
                default:                             key = "default"; break;
            }
            command_hint = as_hint;
        } else {
            // Positional type arg
            for (auto& a : args) {
                if (a.empty() || a[0] == '-') continue;
                key = a;
                break;
            }
            if (key.empty()) {
                std::cerr << "icmg filter: requires <type> or --as \"<cmd>\"\n";
                return 1;
            }
            command_hint = key;  // e.g. for db filter to pick dialect
        }

        auto& reg = core::Registry<icmg::tkil::BaseFilter>::instance();
        if (!reg.has(key)) {
            std::cerr << "icmg filter: unknown filter '" << key
                      << "' (known: git, build, test, search, npm, db, default)\n";
            return 1;
        }
        auto filter = reg.create(key);

        // Slurp stdin
        std::ostringstream buf;
        buf.str(core::slurpStdinSafe());
        std::string raw = buf.str();
        if (raw.empty()) return 0;

        auto result = filter->filter(raw, command_hint);
        std::cout << result.output;
        if (result.was_truncated) {
            std::cerr << "[icmg filter: " << result.filtered_lines << "/"
                      << result.original_lines << " lines kept]\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("filter", FilterCommand);

} // namespace icmg::cli
