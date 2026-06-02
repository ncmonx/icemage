// src/cli/commands/govern_cmd.cpp
// v2.0.0 C1+C3+F1: `icmg govern` — deterministic injection governor.
//   icmg govern --report             : honest per-source fill share (F1) so the
//                                       "rarer compaction" claim is never inflated.
//   icmg govern [--budget <tokens>]  : emit a budgeted, U-ordered working-set built
//                                       from memory decisions + known-issues.
// Zero-model, deterministic. Pure selection lives in core/working_set.hpp.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/working_set.hpp"
#include "../../imem/memory_store.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class GovernCommand : public BaseCommand {
public:
    std::string name() const override { return "govern"; }
    std::string description() const override {
        return "Deterministic injection governor: --report fill share, --budget N emit working-set";
    }
    void usage() const override {
        std::cout << "Usage: icmg govern [--report] [--budget <tokens>]\n"
                  << "  " << description() << "\n";
    }

    int run(const std::vector<std::string>& args) override {
        bool report = false;
        int budget = 4000;  // default injection budget
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--report") report = true;
            else if (args[i] == "--budget" && i + 1 < args.size()) budget = std::stoi(args[++i]);
        }

        // Build candidate sources from memory (decisions + known-issues + rules + plan).
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);
        auto hits = mem.recall("decisions rules known-issue plan", 40, false);

        std::vector<core::Source> candidates;
        candidates.reserve(hits.size());
        for (const auto& h : hits) {
            core::Source s;
            s.id        = h.topic;
            s.text      = h.content;
            s.tokens    = (int)(h.content.size() / 4);  // ~4 chars/token heuristic
            s.relevance = h.score;
            s.priority  = (h.topic.find("decisions") != std::string::npos) ? 2 : 1;
            s.pinned    = (h.pinned != 0);
            candidates.push_back(std::move(s));
        }

        if (report) {
            int total = 0;
            for (const auto& c : candidates) total += c.tokens;
            std::cout << "[govern --report] icmg-injectable candidates: "
                      << candidates.size() << " sources, ~" << total << " tokens.\n"
                      << "Run `icmg context-budget` for the full live-window per-source share.\n"
                      << "F1 honesty: governor only caps icmg-injected context; CC conversation\n"
                      << "turns + UI attachments are outside icmg's control.\n";
            return 0;
        }

        auto ws = core::selectWorkingSet(candidates, budget);
        auto ordered = core::orderUShaped(ws.items);
        std::cout << "[govern] budget=" << budget << " tokens, kept "
                  << ordered.size() << "/" << candidates.size()
                  << " (~" << ws.totalTokens << " tok), U-ordered:\n";
        for (const auto& s : ordered) std::cout << "  - " << s.id << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("govern", GovernCommand);

}  // namespace icmg::cli
