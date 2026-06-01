// v1.80 atomize T7: icmg memory atomize run|status|stats
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/atom_store.hpp"
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>

namespace icmg::cli {

class MemoryAtomizeCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-atomize"; }
    std::string description() const override {
        return "Drain the memory atomization queue (semantic atom layer)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg memory atomize <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  run [--max N]   Drain pending queue (default max=256)\n"
            "  status          Show pending queue depth + atom count\n"
            "  stats           Alias for status\n\n"
            "Env:\n"
            "  ICMG_ATOMIZE=0  Disable atomization (run becomes no-op)\n";
    }

    int run(const std::vector<std::string>& args) override {
        const char* off = std::getenv("ICMG_ATOMIZE");
        bool disabled = off && off[0] == 0x30;

        std::string sub = args.empty() ? "run" : args[0];

        if (sub == "--help" || sub == "-h") { usage(); return 0; }

        if (sub == "status" || sub == "stats") {
            core::Db db(core::Config::instance().projectDbPath("."));
            imem::AtomStore as(db);
            int64_t pending = 0, atoms = 0;
            db.query("SELECT COUNT(*) FROM memory_atom_queue", {},
                [&](const core::Row& r) { if (!r.empty()) pending = std::stoll(r[0]); });
            db.query("SELECT COUNT(*) FROM memory_atoms WHERE deleted_at=0", {},
                [&](const core::Row& r) { if (!r.empty()) atoms = std::stoll(r[0]); });
            std::cout << "atoms: " << atoms
                      << "  pending: " << pending
                      << (disabled ? "  [ICMG_ATOMIZE=0: worker disabled]" : "")
                      << "\n";
            return 0;
        }

        if (sub == "run") {
            if (disabled) {
                std::cout << "atomize: disabled (ICMG_ATOMIZE=0)\n";
                return 0;
            }
            int max = 256;
            for (size_t i = 1; i + 1 < args.size(); ++i) {
                if (args[i] == "--max") {
                    try { max = std::stoi(args[i + 1]); } catch (...) {}
                }
            }
            core::Db db(core::Config::instance().projectDbPath("."));
            imem::AtomStore as(db);
            int n = as.drainQueue(max);
            std::cout << "atomize: processed " << n << " node(s)\n";
            return 0;
        }

        std::cerr << "icmg memory atomize: unknown subcommand: " << sub << "\n";
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("memory-atomize", MemoryAtomizeCommand);

} // namespace icmg::cli
