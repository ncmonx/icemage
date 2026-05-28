// v1.53.0: icmg route classify <prompt> — emit RouteDecision for hooks.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../router.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#ifdef ICMG_USE_LLAMA
#  include "../../llm/llama_runner.hpp"
#  include "../../llm/warm_client.hpp"
#  include "../../llm/warm_pool.hpp"
#  include "../../llm/chat_template.hpp"
#endif

namespace icmg::cli {

namespace {
// v1.63 F6: parse an LLM one-word route answer into a Route. Returns false
// if the text doesn't clearly name a route (caller keeps the regex result).
inline bool parseLlmRoute(const std::string& text, Route& out) {
    std::string up;
    for (char c : text) if (c >= 'a' && c <= 'z') up += char(c - 32);
                        else if (c >= 'A' && c <= 'Z') up += c;
    // First explicit keyword wins.
    auto pos_local = up.find("LOCAL");
    auto pos_cloud = up.find("CLOUD");
    auto pos_cache = up.find("CACHE");
    auto best = std::string::npos; Route r = Route::CLOUD; bool found = false;
    if (pos_local != std::string::npos && (!found || pos_local < best)) { best = pos_local; r = Route::LOCAL; found = true; }
    if (pos_cloud != std::string::npos && (!found || pos_cloud < best)) { best = pos_cloud; r = Route::CLOUD; found = true; }
    if (pos_cache != std::string::npos && (!found || pos_cache < best)) { best = pos_cache; r = Route::CACHE; found = true; }
    if (found) out = r;
    return found;
}
}  // namespace

class RouteCommand : public BaseCommand {
public:
    std::string name()        const override { return "route"; }
    std::string description() const override { return "Smart prompt classifier (LOCAL/CLOUD/CACHE)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg route <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  classify <prompt>           Print routing decision (text).\n"
            "  classify --json <prompt>    Emit decision as JSON.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }
        if (args[0] != "classify") {
            std::cerr << "route: unknown subcommand: " << args[0] << "\n";
            return 2;
        }
        bool json = false;
        size_t idx = 1;
        if (idx < args.size() && args[idx] == "--json") { json = true; ++idx; }
        if (idx >= args.size()) {
            std::cerr << "Usage: icmg route classify [--json] <prompt>\n";
            return 2;
        }
        // Join remaining args into single prompt.
        std::string prompt;
        for (size_t i = idx; i < args.size(); ++i) {
            if (!prompt.empty()) prompt += " ";
            prompt += args[i];
        }
        auto d = classifyPrompt(prompt);
        std::string band = confidenceBandName(confidenceBand(d.confidence));

        // v1.63 F6: LLM escalation on low confidence. Regex stays the
        // sub-ms default; only when the regex result is low-confidence AND
        // ICMG_LLM_ROUTER=1 do we ask the local LLM to break the tie. Any
        // failure (no build, no daemon, unparsable answer) keeps the regex
        // decision — never blocks.
#ifdef ICMG_USE_LLAMA
        if (isLowConfidence(d.confidence)) {
            if (const char* e = std::getenv("ICMG_LLM_ROUTER");
                e && *e && std::string(e) != "0") {
                std::string sys = "You are a routing classifier. Answer with "
                    "exactly one word: LOCAL (trivial/greeting/ack), CLOUD "
                    "(code/debug/complex reasoning), or CACHE (repeat lookup).";
                std::string p = icmg::llm::buildChatMLPrompt(sys, prompt);
                llm::InferParams ip;
                ip.max_tokens = 4; ip.temperature = 0.0f;
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
                Route llm_route;
                if (!ans.empty() && parseLlmRoute(ans, llm_route)) {
                    d.route = llm_route;
                    d.reason = "llm-escalation (regex low-conf)";
                    d.confidence = kConfidenceHigh;  // LLM adjudicated
                    band = "high";
                }
            }
        }
#endif

        const char* route_str = (d.route == Route::LOCAL ? "LOCAL"
                                : d.route == Route::CLOUD ? "CLOUD" : "CACHE");
        if (json) {
            std::cout << "{\"route\":\"" << route_str
                      << "\",\"confidence\":" << d.confidence
                      << ",\"band\":\"" << band << "\""
                      << ",\"reason\":\"" << d.reason << "\"}\n";
        } else {
            std::cout << "route=" << route_str
                      << " confidence=" << d.confidence
                      << " band=" << band
                      << " reason=" << d.reason << "\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("route", RouteCommand);

}  // namespace icmg::cli
