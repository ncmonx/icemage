// v1.63 F8: `icmg mine` — scan recent memory (decisions/fixes) and PROPOSE
// candidate rules. Suggestions ONLY: never writes a rule, never auto-applies.
// The user reviews and runs `icmg store --topic ...` / rule add manually.
//
// Local LLM when available (proposes phrased rules); deterministic
// frequency-heuristic fallback otherwise (most-common topic prefixes ->
// "consider a rule about X"). Output is explicitly tagged as suggestions.
//
//   icmg mine [--limit N] [--max-tokens N]

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"
#include "../mine_logic.hpp"   // v1.63 F8: topicPrefix + heuristicMine (testable)

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef ICMG_USE_LLAMA
#  include "../../llm/llama_runner.hpp"
#  include "../../llm/warm_client.hpp"
#  include "../../llm/warm_pool.hpp"
#  include "../../llm/chat_template.hpp"
#endif

namespace icmg::cli {

class MineCommand : public BaseCommand {
public:
    std::string name()        const override { return "mine"; }
    std::string description() const override {
        return "Propose candidate rules from recurring memory (suggestions only)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg mine [--limit N] [--max-tokens N]\n\n"
            "  Scans recent memory (decisions/fixes) and PROPOSES candidate\n"
            "  rules. Suggestions only — nothing is written or applied.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        int limit = 30;
        try { limit = std::stoi(flagValue(args, "--limit", "30")); } catch (...) {}

        core::Config& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        // Pull recent nodes (all, then keep most-recent `limit` by created_at).
        auto nodes = mem.all();
        std::sort(nodes.begin(), nodes.end(),
                  [](const imem::MemoryNode& a, const imem::MemoryNode& b){
                      return a.created_at > b.created_at;
                  });
        if ((int)nodes.size() > limit) nodes.resize(limit);

        if (nodes.empty()) {
            std::cout << "[mine: no memory yet — store some decisions first]\n";
            return 0;
        }

#ifdef ICMG_USE_LLAMA
        int max_tokens = 256;
        try { max_tokens = std::stoi(flagValue(args, "--max-tokens", "256")); }
        catch (...) {}
        std::ostringstream digest;
        for (const auto& n : nodes)
            digest << "- [" << n.topic << "] " << n.content << "\n";
        std::string sys =
            "You analyse a developer's recent decisions/fixes and propose up "
            "to 3 concise candidate RULES (imperative, <100 chars each) that "
            "capture recurring intent. These are SUGGESTIONS the user will "
            "review — do not assume they are applied. Output a numbered list.";
        std::string p = icmg::llm::buildChatMLPrompt(sys, digest.str());
        llm::InferParams ip;
        ip.max_tokens = max_tokens; ip.temperature = 0.3f;
        ip.stop = icmg::llm::chatMLStopToken();
        std::string ans;
        if (icmg::llm::warmAvailable()) {
            if (auto w = icmg::llm::tryWarmInfer(p, ip,
                    std::chrono::milliseconds(150)); w) ans = w->text;
        }
        if (ans.empty()) {
            std::string err;
            if (auto* run = llm::WarmPool::instance().acquire(err)) {
                auto res = run->infer(p, ip);
                if (res.ok) ans = res.text;
            }
        }
        if (!ans.empty()) {
            std::cout << "[mine: LLM suggestions — review, nothing applied]\n"
                      << ans << "\n";
            return 0;
        }
        // LLM unavailable -> heuristic.
        std::cout << heuristicMine(nodes);
#else
        std::cout << heuristicMine(nodes);
#endif
        return 0;
    }
};

ICMG_REGISTER_COMMAND("mine", MineCommand);

}  // namespace icmg::cli
