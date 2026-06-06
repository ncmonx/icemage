// 2026-06-06: execution-context signal for no-premium LLM routing.
//
// The local LLM (llama.cpp backend) is dead weight in interactive sessions:
// Claude (a premium LLM) is present on every turn and is strictly better, so the
// local model never earns its keep. It should fire ONLY when no premium LLM is
// present in this execution (cron / daemon / headless agent / offline), or when
// the caller explicitly requests local.
//
// This header exposes that single signal: `premiumAvailable()`.
//   - DEFAULT TRUE  = assume premium present = local OFF (safe-by-default).
//   - A no-premium entrypoint (cron/daemon/agent) must explicitly declare itself,
//     via env ICMG_RUN_MODE=headless (cross-process) or setPremiumAvailable(false)
//     (in-process). If nothing declares it, local stays dormant — no regression,
//     no runaway local inference.
#pragma once
#include <string>

namespace icmg::core {

enum class RunMode { INTERACTIVE, HEADLESS };

// Pure helper: map an env string to a RunMode (unknown/empty -> INTERACTIVE).
RunMode parseRunMode(const std::string& s);

// Lazy: first call reads env ICMG_RUN_MODE; later calls return cached/overridden.
RunMode currentRunMode();
void    setRunMode(RunMode m);   // overrides + re-derives premium
bool    isHeadless();

// Premium (Claude/external) availability. DEFAULT TRUE = present = local OFF.
// First read derives from currentRunMode() (HEADLESS => false) unless overridden.
bool premiumAvailable();
void setPremiumAvailable(bool v);

} // namespace icmg::core
