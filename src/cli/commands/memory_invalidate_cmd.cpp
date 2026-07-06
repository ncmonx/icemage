// `icmg memory invalidate <id> [--by <newId>]` -- bi-temporal fact invalidation
// (feature #5, Graphiti pattern). Marks a live memory fact as superseded WITHOUT
// deleting it: recall then skips it, but history/get() keep it. Optionally links
// the replacing fact via --by <newId>.
//
// Registered as "memory-invalidate"; dispatched from MemoryRootCommand
// (`icmg memory invalidate ...`). Extends the existing memory command surface
// rather than adding a parallel top-level command (anti-dup reflex).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"

#include <iostream>
#include <string>

namespace icmg::cli {

class MemoryInvalidateCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-invalidate"; }
    std::string description() const override {
        return "Mark a memory fact as superseded (bi-temporal; kept for history)";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            std::cout << "Usage: icmg memory invalidate <id> [--by <newId>]\n"
                      << "  Supersede a live fact: recall skips it, history keeps it.\n"
                      << "  --by <newId>  id of the fact that replaces it (optional).\n";
            return args.empty() ? 1 : 0;
        }
        int64_t id;
        try { id = std::stoll(args[0]); } catch (...) {
            std::cerr << "icmg memory invalidate: invalid id\n";
            return 1;
        }
        int64_t by = 0;
        std::string byStr = flagValue(args, "--by");
        if (!byStr.empty()) { try { by = std::stoll(byStr); } catch (...) {} }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);

        if (store.get(id).id == 0) {
            std::cerr << "icmg memory invalidate: not found (#" << id << ")\n";
            return 1;
        }
        if (!store.invalidate(id, by)) {
            std::cerr << "icmg memory invalidate: #" << id
                      << " already invalidated (no change)\n";
            return 1;
        }
        std::cout << "Invalidated #" << id;
        if (by > 0) std::cout << " (superseded by #" << by << ")";
        std::cout << " -- kept for history, excluded from recall.\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("memory-invalidate", MemoryInvalidateCommand);

} // namespace icmg::cli
