// Phase 67 T20: `icmg tool-budget` — per-turn tool-call counter.
//
// PreToolUse hook increments counter at ~/.icmg/tool-counter-<session>.txt.
// When count > limit, hook returns deny with reason. Resets via Stop hook.
//
// Prevents 30-tool runaway turns that explode tokens.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace icmg::cli {

class ToolBudgetCommand : public BaseCommand {
public:
    std::string name()        const override { return "tool-budget"; }
    std::string description() const override {
        return "Per-turn tool-call budget gate (prevents runaway tool loops)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg tool-budget <action>\n\n"
            "Actions:\n"
            "  set <N>     Set per-turn limit (writes ~/.icmg/tool-budget.txt)\n"
            "  get         Show current limit\n"
            "  status      Show counter for current turn\n"
            "  reset       Reset counter (called by Stop hook)\n"
            "  check       Increment + check; exit 1 if over budget (PreToolUse hook)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];

        fs::path home = homeDir();
        fs::path budget_path  = home / ".icmg" / "tool-budget.txt";
        fs::path counter_path = home / ".icmg" / "tool-counter.txt";

        if (action == "set") {
            if (args.size() < 2) { std::cerr << "icmg tool-budget set: requires <N>\n"; return 1; }
            std::error_code ec; fs::create_directories(budget_path.parent_path(), ec);
            std::ofstream f(budget_path);
            f << args[1] << "\n";
            std::cout << "icmg tool-budget: limit set to " << args[1] << " calls/turn\n";
            return 0;
        }
        if (action == "get") {
            int limit = readBudget(budget_path);
            std::cout << "icmg tool-budget: " << limit << " calls/turn\n";
            return 0;
        }
        if (action == "status") {
            int limit = readBudget(budget_path);
            int count = readCounter(counter_path);
            std::cout << "Counter: " << count << "/" << limit << "\n";
            return 0;
        }
        if (action == "reset") {
            std::error_code ec;
            fs::remove(counter_path, ec);
            return 0;
        }
        if (action == "check") {
            int limit = readBudget(budget_path);
            if (limit <= 0) return 0;  // disabled
            int count = readCounter(counter_path) + 1;
            std::error_code ec; fs::create_directories(counter_path.parent_path(), ec);
            std::ofstream f(counter_path);
            f << count << "\n";
            if (count > limit) {
                std::cerr << "[icmg tool-budget] " << count << " > " << limit
                          << " — limit reached. Reset with `icmg tool-budget reset` "
                          << "or raise via `icmg tool-budget set N`.\n";
                return 1;
            }
            return 0;
        }

        std::cerr << "icmg tool-budget: unknown action '" << action << "'\n";
        usage();
        return 1;
    }

private:
    static fs::path homeDir() {
        const char* h = std::getenv("USERPROFILE");
        if (!h) h = std::getenv("HOME");
        return fs::path(h ? h : ".");
    }
    static int readBudget(const fs::path& p) {
        if (!fs::exists(p)) return 0;
        std::ifstream f(p); int v = 0; f >> v; return v;
    }
    static int readCounter(const fs::path& p) {
        if (!fs::exists(p)) return 0;
        std::ifstream f(p); int v = 0; f >> v; return v;
    }
};

ICMG_REGISTER_COMMAND("tool-budget", ToolBudgetCommand);

} // namespace icmg::cli
