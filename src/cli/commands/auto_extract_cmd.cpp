// 2026-06-07: `icmg auto-extract` — Layer-0 rule-based memory capture (luna idea, STANDOUT).
// Zero-LLM: a PostToolUse hook pipes a tool's output on stdin and passes --cmd/--exit; the
// pure classifier decides whether to persist (git commit -> auto:wflog, error -> auto:error).
// Low-importance (decays faster); clearly namespaced so it never masquerades as a human note.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/stdin_util.hpp"
#include "../../imem/memory_store.hpp"
#include "../../imem/memory_node.hpp"
#include "../../imem/layer0_extract.hpp"
#include "../../imem/entity_extract.hpp"   // #luna-batch: Layer-0 entity enrichment
#include <iostream>
#include <string>

namespace icmg::cli {

class AutoExtractCommand : public BaseCommand {
public:
    std::string name()        const override { return "auto-extract"; }
    std::string description() const override {
        return "Layer-0 zero-LLM memory capture from tool events (git commit / errors)"; }
    void usage() const override {
        std::cout <<
            "Usage: icmg auto-extract --cmd \"<cmd>\" [--exit N] [--output \"...\"]\n\n"
            "Reads tool output from stdin (or --output), classifies via pure rules, and\n"
            "persists high-signal events only (successful git commit, failing-build error).\n"
            "Designed as a PostToolUse hook. Skips silently when nothing worth storing.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        std::string cmd = flagValue(args, "--cmd");
        int exitCode = 0;
        try { exitCode = std::stoi(flagValue(args, "--exit", "0")); } catch (...) {}
        std::string output = flagValue(args, "--output");
        if (output.empty()) output = core::slurpStdinSafe();

        auto r = imem::classifyToolEvent(cmd, output, exitCode);
        if (r.kind == "skip") return 0;   // silent: no noise

        imem::MemoryNode n;
        n.importance = 1;                  // low: auto-captured, decays faster
        if (r.kind == "wflog") {
            n.topic = "auto:wflog"; n.keywords = "auto layer0 wflog commit";
        } else {                           // known-issue
            n.topic = "auto:error"; n.keywords = "auto layer0 error build";
        }
        n.content = r.content;
        n.source  = "layer0";
        // Enrich keywords with rule-based entities (URL/IP/env/mention) from the full output,
        // so an auto-captured event is searchable by the things it references. Zero-LLM.
        for (const auto& ent : imem::extractEntities(output)) n.keywords += " " + ent;

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        try {
            int64_t id = store.store(n, /*force=*/true);
            std::cout << "[layer0] " << r.kind << " #" << id << ": " << r.content << "\n";
        } catch (...) { return 0; }        // fail-open: never break the hook
        return 0;
    }
};

ICMG_REGISTER_COMMAND("auto-extract", AutoExtractCommand);

} // namespace icmg::cli
