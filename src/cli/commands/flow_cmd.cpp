// `icmg flow` — run a named composite chain of existing icmg commands, so related
// features interlink (one command triggers the whole sequence) instead of being
// invoked one at a time and forgotten.
//   icmg flow list                 list built-in flows
//   icmg flow <name> ["<arg>"]     run a flow ({ARG} steps get <arg>)
//   icmg flow <name> --dry-run     print the steps without executing
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/flow_registry.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class FlowCommand : public BaseCommand {
public:
    std::string name() const override { return "flow"; }
    std::string description() const override {
        return "Run a named chain of existing commands (interlinked workflow)";
    }
    void usage() const override {
        std::cout << "Usage: icmg flow list | <name> [\"<arg>\"] [--dry-run]\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "list") { listFlows(); return 0; }

        const std::string flowName = args[0];
        bool dry = false;
        std::string arg;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--dry-run") dry = true;
            else if (!args[i].empty() && args[i][0] != '-' && arg.empty()) arg = args[i];
        }

        const core::Flow* f = core::findFlow(flowName);
        if (!f) {
            std::cerr << "unknown flow: " << flowName << "\n";
            listFlows();
            return 1;
        }

        auto steps = core::substituteArg(f->steps, arg);
        auto& reg = core::Registry<BaseCommand>::instance();

        for (size_t i = 0; i < steps.size(); ++i) {
            const auto& step = steps[i];
            std::string line = "icmg";
            for (const auto& t : step) line += " " + t;
            std::cout << "[flow " << flowName << " " << (i + 1) << "/" << steps.size()
                      << "] " << line << "\n";
            if (dry) continue;

            auto cmd = reg.create(step[0]);
            if (!cmd) { std::cerr << "  (no such command: " << step[0] << ")\n"; return 1; }
            std::vector<std::string> sub(step.begin() + 1, step.end());
            int rc = cmd->run(sub);
            if (rc != 0) {
                std::cerr << "[flow] step " << (i + 1) << " failed (rc=" << rc
                          << "); stopping.\n";
                return rc;
            }
        }
        return 0;
    }

private:
    void listFlows() const {
        std::cout << "Available flows:\n";
        for (const auto& f : core::builtinFlows())
            std::cout << "  " << f.name << "  -- " << f.desc << "\n";
    }
};

ICMG_REGISTER_COMMAND("flow", FlowCommand);

}  // namespace icmg::cli
