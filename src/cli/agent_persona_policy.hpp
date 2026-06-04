#pragma once
// Policy: should `icmg agent` prepend the user's chat-persona to the SUB-AGENT prompt?
// A coding sub-agent (--exec, file-edit + shell) must be a focused, clean engineer:
// a chat-persona (any flavor) adds noise, can drift tone, and -- worst case -- an
// inappropriate persona on an autonomous agent causes refusals/odd behavior.
//   - --exec            : NEVER persona (hard rule).
//   - advisory (default): OFF; opt-in via config agent.use_persona=true.
//   - ICMG_NO_PERSONA=1 : force off regardless.
// Pure + header-only so it is unit-testable without Win32/CLI/config plumbing.

namespace icmg::cli {

inline bool agentUsePersona(bool exec, bool no_persona_env, bool cfg_use_persona) {
    if (exec)          return false;   // coding sub-agent = clean engineer, always
    if (no_persona_env) return false;  // explicit opt-out
    return cfg_use_persona;            // default false -> opt-in only
}

}  // namespace icmg::cli
