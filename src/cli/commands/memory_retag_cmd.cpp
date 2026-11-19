// `icmg memory retag (<id>[,<id>...] | --topic <prefix>) --private|--team`
// (#288). Changes who can see memories when several OS accounts share one
// project folder. Only the caller's own rows and author-less legacy rows
// (claimed by the caller) change; other accounts' rows are never touched.
//
// Registered as "memory-retag"; dispatched from MemoryRootCommand
// (`icmg memory retag ...`), same pattern as memory-invalidate.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/memory_visibility.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

class MemoryRetagCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-retag"; }
    std::string description() const override {
        return "Set memory visibility (private/team) for shared project folders";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            std::cout << "Usage: icmg memory retag <id>[,<id>...] --private|--team\n"
                      << "       icmg memory retag --topic <prefix> --private|--team\n"
                      << "  private = only you see it; team = everyone using this project.\n"
                      << "  Changes your own memories and untagged older ones (which become\n"
                      << "  yours). Other accounts' memories are never changed.\n";
            return args.empty() ? 1 : 0;
        }
        const bool priv = hasFlag(args, "--private");
        const bool team = hasFlag(args, "--team");
        if (priv == team) {
            std::cerr << "icmg memory retag: give exactly one of --private or --team\n";
            return 2;
        }
        const std::string topic = flagValue(args, "--topic");
        std::vector<int64_t> ids;
        if (topic.empty()) {
            if (args[0].empty() || args[0][0] == '-') {
                std::cerr << "icmg memory retag: need <id>[,<id>...] or --topic <prefix>\n";
                return 2;
            }
            std::stringstream ss(args[0]);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                try { ids.push_back(std::stoll(tok)); } catch (...) {
                    std::cerr << "icmg memory retag: invalid id '" << tok << "'\n";
                    return 2;
                }
            }
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        core::ensureVisibilityColumns(db);
        const char* vis = priv ? core::kVisibilityPrivate : core::kVisibilityTeam;
        const int n = core::retagMemories(db, ids, topic, vis);
        std::cout << "Retagged " << n << " memor" << (n == 1 ? "y" : "ies") << " as " << vis << ".\n";
        if (n == 0)
            std::cout << "(Nothing matched among your own or untagged memories.)\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("memory-retag", MemoryRetagCommand);

} // namespace icmg::cli
