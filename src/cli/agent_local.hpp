// 2026-06-06: pure decision helpers for `icmg agent` native-local backend.
// Unit-testable, no model dependency.
//
// The local LLM serves `icmg agent` ONLY when no premium LLM is present
// (cron/daemon/offline) or the caller explicitly requests local (--local).
// It is ADVISORY-ONLY: --exec (file edit / shell) is refused on the local
// route — a weak local model must never auto-edit files; --exec needs a
// premium agentic CLI.
#pragma once
#include <string>

namespace icmg::cli {

struct AgentLocalDecision {
    bool use_local   = false;  // route to in-process WarmPool advisory
    bool refuse_exec = false;  // --exec attempted on a local route -> refuse
    std::string reason;
};

// Local fires when (!premium || explicit_local). If that holds AND exec is
// requested, it is refused (advisory-only).
inline AgentLocalDecision agentLocalDecision(bool premium_available,
                                             bool explicit_local,
                                             bool exec) {
    AgentLocalDecision d;
    bool local_route = (!premium_available || explicit_local);
    if (local_route && exec) {
        d.use_local   = false;
        d.refuse_exec = true;
        d.reason = "local backend is advisory-only; --exec requires a premium agentic CLI";
        return d;
    }
    d.use_local = local_route;
    d.reason = local_route ? "no premium / explicit -> local advisory"
                           : "premium present -> external CLI";
    return d;
}

// Rough char-budget = max_tokens * 4. Head-preserving truncation; sets warned.
inline std::string truncatePromptToWindow(const std::string& prompt,
                                          int max_tokens, bool& warned) {
    const std::size_t budget = static_cast<std::size_t>(max_tokens) * 4;
    if (prompt.size() <= budget) { warned = false; return prompt; }
    warned = true;
    return prompt.substr(0, budget);
}

} // namespace icmg::cli
