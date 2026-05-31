// v1.79.0 ICM dual-memory: `icmg atomize` — drain the memory atomization queue.
// Thin shell over imem::AtomStore::drainQueue (tested in test_atom_store.cpp).
//   icmg atomize run [--max N]   drain up to N queued nodes (default 256)
//   icmg atomize status          show atom count + pending queue depth
// Opt-out: ICMG_ATOMIZE=0 makes `run` a no-op.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/atom_store.hpp"
#include <iostream>
#include <cstdlib>
#include <string>

namespace icmg::cli {

class AtomizeCommand : public BaseCommand {
public:
    std::string name()        const override { return "atomize"; }
    std::string description() const override { return "Drain the memory atomization queue (semantic atom layer)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg atomize [run|status] [--max N]\n\n"
            "  run [--max N]   Drain up to N queued nodes (default 256) into atoms\n"
            "  status          Show atom count + pending queue depth\n\n"
            "Opt-out: ICMG_ATOMIZE=0 disables atomization (run becomes a no-op).\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }

        std::string sub = "run";
        for (auto& a : args) { if (!a.empty() && a[0] != '-') { sub = a; break; } }

        core::Db db(core::Config::instance().projectDbPath("."));

        if (sub == "status" || sub == "stats") {
            int atoms = 0, pending = 0;
            db.query("SELECT COUNT(*) FROM memory_atoms WHERE deleted_at=0", {},
                     [&](const core::Row& r) { if (!r.empty()) atoms = std::stoi(r[0]); });
            db.query("SELECT COUNT(*) FROM memory_atom_queue", {},
                     [&](const core::Row& r) { if (!r.empty()) pending = std::stoi(r[0]); });
            std::cout << "atoms: " << atoms << "  pending: " << pending << "\n";
            return 0;
        }

        // run (default)
        if (const char* off = std::getenv("ICMG_ATOMIZE"); off && off[0] == '0') {
            std::cout << "atomize: disabled (ICMG_ATOMIZE=0)\n";
            return 0;
        }
        int max = 256;
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == "--max") { try { max = std::stoi(args[i + 1]); } catch (...) {} }

        imem::AtomStore as(db);
        int n = as.drainQueue(max);
        std::cout << "atomize: processed " << n << " node(s)\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("atomize", AtomizeCommand);

} // namespace icmg::cli
