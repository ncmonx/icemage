// `icmg memory link <id> --to <id> [--relation <rel>]` -- causal-fact retrieval
// (feature #1). Creates a typed causal edge between two memory facts, layered
// OVER BM25 recall. `icmg recall --causal` then walks these edges 1-hop.
//
// Registered as "memory-link"; dispatched from MemoryRootCommand
// (`icmg memory link ...`). Extends the memory command surface (anti-dup).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class MemoryLinkCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-link"; }
    std::string description() const override {
        return "Link two memory facts with a typed causal edge (for recall --causal)";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            std::cout << "Usage: icmg memory link <id> --to <id> [--relation <rel>]\n"
                      << "  Create a causal edge src --relation--> dst.\n"
                      << "  --relation: caused_by | enables | blocks | related_to (default) |\n"
                      << "              depends_on | contradicts | supersedes\n"
                      << "  Recall walks these edges via: icmg recall <q> --causal\n";
            return args.empty() ? 1 : 0;
        }
        int64_t src, dst;
        try { src = std::stoll(args[0]); dst = std::stoll(flagValue(args, "--to")); }
        catch (...) {
            std::cerr << "icmg memory link: need <id> --to <id> (numeric)\n";
            return 1;
        }
        std::string rel = flagValue(args, "--relation", "related_to");
        static const std::vector<std::string> valid = {
            "caused_by", "enables", "blocks", "related_to",
            "depends_on", "contradicts", "supersedes"
        };
        bool ok = false;
        for (auto& r : valid) if (r == rel) { ok = true; break; }
        if (!ok) {
            std::cerr << "icmg memory link: invalid --relation '" << rel
                      << "'. Use --help for valid types.\n";
            return 1;
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);

        if (store.get(src).id == 0) {
            std::cerr << "icmg memory link: source #" << src << " not found\n";
            return 1;
        }
        if (store.get(dst).id == 0) {
            std::cerr << "icmg memory link: target #" << dst << " not found\n";
            return 1;
        }
        store.linkCausal(src, dst, rel);
        std::cout << "Linked #" << src << " --" << rel << "--> #" << dst
                  << "  (recall --causal will walk it)\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("memory-link", MemoryLinkCommand);

} // namespace icmg::cli
