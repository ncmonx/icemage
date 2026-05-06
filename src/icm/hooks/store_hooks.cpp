#include "../../core/hook_bus.hpp"
#include "../../core/registry.hpp"
#include <string>
#include <regex>

namespace icmg::icm {

// PRE_STORE hook: detect abbreviation patterns in content/topic
// Patterns: "bkm=bukti kas masuk", "bkm:...", "(bkm) ..."
// If detected, annotate context with abbreviation hint (non-blocking)
static void storeAbbreviationDetector(core::HookContext& ctx) {
    auto content = ctx.get<std::string>("content", "");
    auto topic   = ctx.get<std::string>("topic",   "");

    static const std::regex pat_eq(R"(\b([A-Za-z]{2,8})=([^,\n]+))");
    static const std::regex pat_colon(R"(\b([A-Za-z]{2,8}):([^,\n]{3,}))");
    static const std::regex pat_paren(R"(\(([A-Za-z]{2,8})\)\s+(\S.+))");

    std::string combined = topic + " " + content;
    std::smatch m;

    // Check for "abbr=expansion" pattern
    if (std::regex_search(combined, m, pat_eq)) {
        ctx.set<std::string>("hint_abbr",      m[1].str());
        ctx.set<std::string>("hint_expansion", m[2].str());
        return;
    }
    // Check for "(abbr) expansion" pattern
    if (std::regex_search(combined, m, pat_paren)) {
        ctx.set<std::string>("hint_abbr",      m[1].str());
        ctx.set<std::string>("hint_expansion", m[2].str());
        return;
    }
    // Check for "abbr:expansion" (only if expansion looks non-trivial)
    if (std::regex_search(combined, m, pat_colon)) {
        ctx.set<std::string>("hint_abbr",      m[1].str());
        ctx.set<std::string>("hint_expansion", m[2].str());
    }
}

// Register at priority 10 (runs before default priority 50)
ICMG_REGISTER_HOOK(core::HookEvent::PRE_STORE, storeAbbreviationDetector, 10);

} // namespace icmg::icm
