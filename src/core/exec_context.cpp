// 2026-06-06: execution-context signal impl. See exec_context.hpp.
#include "exec_context.hpp"
#include <cstdlib>
#include <optional>

namespace icmg::core {

RunMode parseRunMode(const std::string& s) {
    return s == "headless" ? RunMode::HEADLESS : RunMode::INTERACTIVE;
}

// Process-global, single-writer-at-startup. Readers after.
static std::optional<RunMode> g_mode;
static std::optional<bool>    g_premium;

RunMode currentRunMode() {
    if (!g_mode) {
        const char* e = std::getenv("ICMG_RUN_MODE");
        g_mode = parseRunMode(e ? e : "");
    }
    return *g_mode;
}

void setRunMode(RunMode m) {
    g_mode = m;
    g_premium.reset();   // re-derive premium from the new mode on next read
}

bool isHeadless() { return currentRunMode() == RunMode::HEADLESS; }

bool premiumAvailable() {
    if (!g_premium) g_premium = (currentRunMode() != RunMode::HEADLESS);
    return *g_premium;
}

void setPremiumAvailable(bool v) { g_premium = v; }

} // namespace icmg::core
