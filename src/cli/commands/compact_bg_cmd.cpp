// v1.32.0 A9a: `icmg compact-bg` — proactive memory compaction worker.
//
// Pull recent decisions / session-snapshots from memory_nodes, summarize
// via local LLM (warm-pool, router-gated) or regex distill fallback,
// store the summary as a new memory_node with topic prefix
// `auto-compact-<ts>` so the next SessionStart wake-up surfaces it.
//
// Invoked manually (`icmg compact-bg`) OR detached by future
// UserPromptSubmit hook (A9b) when context fill >= --threshold percent.
// This is the WARM/COLD path worker — never call from hot hooks.
//
// Latency: depends on warm-pool state.
//   - LLM hot (warm-pool loaded):       ~1-3 s
//   - LLM cold (first load this proc):  ~5-12 s (model load + infer)
//   - Regex fallback:                   <100 ms
#include "../base_command.hpp"
#include "../compaction_digest.hpp"
#include "../../core/utf8_prefix.hpp"
#include "../../core/registry.hpp"
#include "../../core/db.hpp"
#include "../../core/config.hpp"
#include "../../core/exec_context.hpp"   // premiumAvailable() / isHeadless()
#include "../../imem/memory_store.hpp"
#include "../../imem/atom_store.hpp"
#include "../../llm/warm_pool.hpp"
#include "../../llm/smart_router.hpp"
#include "../../llm/llama_runner.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

namespace {

std::string nowStamp() {
    auto t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", std::localtime(&t));
    return std::string(buf);
}

// Extractive distill: verbatim first sentence per hit + its source id
// (truncate/drop only, never rephrase -- compaction_digest.hpp).
std::string regexDistill(const std::vector<imem::MemoryNode>& hits) {
    return "auto-compact regex-distill " + nowStamp() + "\n"
         + extractiveDigest(hits, kCompactDigestLines, kCompactSentenceBytes);
}

} // namespace

class CompactBgCommand : public BaseCommand {
public:
    std::string name()        const override { return "compact-bg"; }
    std::string description() const override {
        return "Proactive memory compaction worker (LLM summarize or regex distill)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg compact-bg [--threshold N] [--limit N] [--query Q]\n\n"
            "Options:\n"
            "  --threshold N   Context-fill % that triggered the call (informational; default 0)\n"
            "  --limit N       Recall N recent hits to summarize (default 20)\n"
            "  --query Q       Recall query string (default \"session decision plan\")\n"
            "  --force-regex   Skip LLM path; always use regex distill\n"
            "  --dry-run       Print summary to stdout; do NOT store as memory_node\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }

        int  limit       = 20;
        int  threshold   = 0;
        bool dry_run     = hasFlag(args, "--dry-run");
        bool force_regex = hasFlag(args, "--force-regex");
        std::string query = flagValue(args, "--query", "session decision plan");
        try { limit     = std::stoi(flagValue(args, "--limit", "20")); }     catch (...) {}
        try { threshold = std::stoi(flagValue(args, "--threshold", "0")); }  catch (...) {}

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);
        // Never compact a compaction: over-fetch, then drop auto-compact-* /
        // session-snapshot* / session:* so a summary is always rebuilt from
        // originals (2026-05-31: two LLM nodes restated each other 2 min apart).
        auto hits = compactionInputs(
            mem.recall(query, limit * kCompactRecallOverfetch, false), (size_t)limit);
        if (hits.empty()) {
            std::cerr << "icmg compact-bg: no hits to summarize for query=\"" << query << "\"\n";
            return 0;
        }

        std::string summary;
        bool used_llm = false;
        std::string used_method = "regex";

        if (!force_regex) {
            llm::CallContext rctx;
            rctx.tier             = llm::PathTier::COLD;
            rctx.kind             = "compact";
            rctx.input_tokens_est = 0;
            for (const auto& h : hits) rctx.input_tokens_est += (h.topic.size() + h.content.size()) / 4;
            rctx.build_has_llama  = llm::LlamaRunner::available();
            rctx.llm_loaded       = llm::WarmPool::instance().isLoaded();
            const char* dis = std::getenv("ICMG_LLM_USER_DISABLED");
            rctx.user_disabled = (dis && *dis == '1');
            // 2026-06-06 no-premium gate: COLD compact uses local LLM only when
            // running headless (daemon/cron). In an interactive Claude session a
            // premium LLM is present -> route REGEX (Claude summarizes instead).
            rctx.premium_available = !icmg::core::isHeadless();
            auto rd = llm::routeFor(rctx);
            if (rd.route == llm::Route::LLM_LOCAL) {
                std::string err;
                llm::LlamaRunner* run = llm::WarmPool::instance().acquire(err);
                if (run) {
                    std::ostringstream pp;
                    pp << "You compress AI coding session memory. Summarize the "
                       << "following " << hits.size() << " entries into <=8 bullet "
                       << "lines of durable facts (decisions, fixes, anti-patterns). "
                       << "Drop chit-chat. Output format: `- <topic>: <fact>`. No prose.\n\n";
                    for (size_t i = 0; i < hits.size(); ++i) {
                        std::string body = hits[i].content;
                        if (body.size() > kCompactLlmBodyBytes)
                            body = core::utf8Prefix(body, kCompactLlmBodyBytes - 3) + "...";
                        pp << (i + 1) << ". " << hits[i].topic << ": " << body << "\n";
                    }
                    pp << "\nSummary:\n";
                    llm::InferParams ip;
                    ip.max_tokens = 384;
                    ip.temperature = 0.2f;
                    auto res = run->infer(pp.str(), ip);
                    if (res.ok && !res.text.empty()) {
                        summary = "auto-compact LLM " + nowStamp() + "\n" + res.text;
                        used_llm = true;
                        used_method = "llm";
                    } else {
                        std::cerr << "[compact-bg] LLM failed (" << res.error
                                  << ") — falling back to regex.\n";
                    }
                } else {
                    std::cerr << "[compact-bg] warm-pool acquire failed (" << err
                              << ") — regex fallback.\n";
                }
            } else {
                std::cerr << "[compact-bg] router routed regex: " << rd.reason << "\n";
            }
        }
        if (summary.empty()) summary = regexDistill(hits);
        // Provenance: every line must be traceable to its originals (the
        // 2026-05-31 LLM node carried a "Postgres" fact found in no memory row).
        if (!summary.empty() && summary.back() != '\n') summary += "\n";
        summary += sourceIdsLine(hits);

        if (dry_run) {
            std::cout << "=== compact-bg dry-run (method=" << used_method
                      << " threshold=" << threshold << "% hits=" << hits.size()
                      << ") ===\n" << summary << "\n";
            return 0;
        }

        // Store as a new memory_node so SessionStart wake-up can surface it.
        imem::MemoryNode n;
        n.topic       = std::string("auto-compact-") + nowStamp();
        n.content     = summary;
        n.importance  = 1;
        n.zone        = "default";
        try {
            int64_t id = mem.store(n, /*force=*/true);
            std::cout << "{\"ok\":true,\"id\":" << id
                      << ",\"method\":\"" << used_method
                      << "\",\"hits\":" << hits.size()
                      << ",\"threshold\":" << threshold
                      << ",\"bytes\":" << summary.size()
                      << "}\n";
        } catch (const std::exception& e) {
            std::cerr << "icmg compact-bg: store failed: " << e.what() << "\n";
            return 2;
        }
        // T8: bounded atom-drain on every compact-bg tick.
        if (!dry_run) {
            const char* no_at = std::getenv("ICMG_ATOMIZE");
            if (!no_at || no_at[0] != '0') {
                try { imem::AtomStore(db).drainQueue(64); } catch (...) {}
            }
        }
        (void)used_llm;
        return 0;
    }
};

ICMG_REGISTER_COMMAND("compact-bg", CompactBgCommand);

} // namespace icmg::cli
