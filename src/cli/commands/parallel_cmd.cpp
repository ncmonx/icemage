// Phase 21 Task 5b: icmg parallel — subprocess fan-out CLI.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/parallel.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

namespace icmg::cli {

class ParallelCommand : public BaseCommand {
public:
    std::string name()        const override { return "parallel"; }
    std::string description() const override { return "Run multiple commands concurrently (subprocess fan-out)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg parallel [options] --task \"<cmd>\" [--task \"<cmd>\" ...]\n\n"
            "Options:\n"
            "  --task \"<cmd>\"           Add a task (repeatable)\n"
            "  --max-concurrency N      Cap concurrent children (default = cpu_count, hard cap 32)\n"
            "  --timeout-ms N           Per-task timeout (default 60000)\n"
            "  --fail-fast              Cancel pending tasks on first non-zero exit\n"
            "  --merge json|concat|none JSON top-level array merge / newline concat / per-task block (default json)\n"
            "  --json                   Full result JSON (per-task exit/stdout/stderr/duration)\n\n"
            "Examples:\n"
            "  icmg parallel --task \"icmg recall foo --json\" --task \"icmg recall bar --json\"\n"
            "  icmg parallel --task \"ctest\" --task \"npm run lint\" --fail-fast --merge none\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage();
            return 0;
        }

        std::vector<core::ParallelTask> tasks;
        int max_conc = 0;
        int timeout_ms = 60000;
        bool fail_fast = hasFlag(args, "--fail-fast");
        std::string merge = flagValue(args, "--merge", "json");
        bool full_json = hasFlag(args, "--json");

        try { max_conc   = std::stoi(flagValue(args, "--max-concurrency", "0")); } catch (...) {}
        try { timeout_ms = std::stoi(flagValue(args, "--timeout-ms", "60000")); } catch (...) {}

        // Collect repeated --task <cmd>
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--task" && i + 1 < args.size()) {
                core::ParallelTask t;
                t.command    = args[++i];
                t.timeout_ms = timeout_ms;
                t.id         = "t" + std::to_string(tasks.size());
                tasks.push_back(std::move(t));
            }
        }

        if (tasks.empty()) {
            std::cerr << "icmg parallel: at least one --task required\n";
            return 1;
        }

        auto results = core::parallel(tasks, max_conc, fail_fast);

        // Compute aggregate exit code = max(child exits)
        int agg_exit = 0;
        for (auto& r : results) if (r.exit_code > agg_exit) agg_exit = r.exit_code;

        if (full_json) {
            nlohmann::json arr = nlohmann::json::array();
            for (auto& r : results) {
                arr.push_back({
                    {"id",          r.id},
                    {"exit_code",   r.exit_code},
                    {"duration_ms", r.duration_ms},
                    {"skipped",     r.skipped},
                    {"stdout",      r.stdout_str},
                    {"stderr",      r.stderr_str}
                });
            }
            std::cout << arr.dump(2) << "\n";
            return agg_exit;
        }

        if (merge == "json") {
            // Try to parse each child stdout as JSON; merge arrays / objects-into-array.
            nlohmann::json combined = nlohmann::json::array();
            bool any_invalid = false;
            for (auto& r : results) {
                if (r.skipped || r.stdout_str.empty()) continue;
                try {
                    auto j = nlohmann::json::parse(r.stdout_str);
                    if (j.is_array()) {
                        for (auto& item : j) combined.push_back(item);
                    } else {
                        combined.push_back(j);
                    }
                } catch (...) {
                    any_invalid = true;
                    break;
                }
            }
            if (any_invalid) {
                // Fall back to concat with separator
                bool first = true;
                for (auto& r : results) {
                    if (r.skipped) continue;
                    if (!first) std::cout << "\n--- [" << r.id << " exit=" << r.exit_code << "] ---\n";
                    first = false;
                    std::cout << r.stdout_str;
                }
            } else {
                std::cout << combined.dump(2) << "\n";
            }
        } else if (merge == "concat") {
            for (auto& r : results) {
                if (!r.skipped) std::cout << r.stdout_str;
            }
        } else {
            // none → per-task header + body
            for (auto& r : results) {
                std::cout << "[" << r.id << " exit=" << r.exit_code
                          << " " << r.duration_ms << "ms"
                          << (r.skipped ? " skipped" : "") << "]\n";
                std::cout << r.stdout_str;
                if (!r.stderr_str.empty()) {
                    std::cout << "[stderr]\n" << r.stderr_str;
                }
                std::cout << "\n";
            }
        }

        return agg_exit;
    }
};

ICMG_REGISTER_COMMAND("parallel", ParallelCommand);

} // namespace icmg::cli
