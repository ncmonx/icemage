// Phase 67 T31: `icmg memory prune-old --topic <prefix> --older Nd`
// Rotates auto-grown topics (auto:/session:/correction:/fail:/distilled:)
// to keep memory_nodes table from unbounded growth.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include <ctime>
#include <iostream>
#include <string>

namespace icmg::cli {

class MemoryPruneOldCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-prune-old"; }
    std::string description() const override {
        return "Delete memory rows by topic prefix older than --older window";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg memory prune-old [options]\n\n"
            "Options:\n"
            "  --topic PREFIX     Required. e.g. 'auto:%' / 'session:%' / 'fail:%'\n"
            "  --older Nd         Delete older than N days (default 60d)\n"
            "  --dry-run          Show count, do not delete\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        std::string topic = flagValue(args, "--topic");
        if (topic.empty()) {
            std::cerr << "icmg memory prune-old: --topic <prefix> required\n";
            return 1;
        }
        std::string older = flagValue(args, "--older", "60d");
        int days = 60;
        if (!older.empty() && older.back() == 'd') {
            try { days = std::stoi(older.substr(0, older.size() - 1)); } catch (...) {}
        }
        bool dry = hasFlag(args, "--dry-run");
        int64_t cutoff = (int64_t)std::time(nullptr) - (int64_t)days * 86400;

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        int64_t count = 0;
        db.query(
            "SELECT COUNT(*) FROM memory_nodes WHERE topic LIKE ? AND last_used < ?",
            {topic, std::to_string(cutoff)},
            [&](const core::Row& r){ if (!r.empty()) count = std::stoll(r[0]); });

        std::cout << "icmg memory prune-old: " << count << " row(s) match topic '"
                  << topic << "' older than " << days << "d\n";
        if (dry) {
            std::cout << "  (dry-run; no deletes applied)\n";
            return 0;
        }
        if (count == 0) return 0;
        db.run("DELETE FROM memory_nodes WHERE topic LIKE ? AND last_used < ?",
               {topic, std::to_string(cutoff)});
        std::cout << "  Deleted " << count << " row(s).\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("memory-prune-old", MemoryPruneOldCommand);

} // namespace icmg::cli
