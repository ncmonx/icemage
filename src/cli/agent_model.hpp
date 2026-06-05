// Token routing for `icmg agent`: pick a cheaper model for bounded tasks.
//
// The sub-agent reloads full context per call (~24k input even for trivial
// work), so the model tier dominates cost. `--light` routes to a cheap model
// (~12x cheaper than the default opus tier); `--model X` is an explicit
// override that always wins. Pure + header-only so it is trivially testable.
#pragma once

#include <string>

namespace icmg::cli {

// Cheap tier for bounded/mechanical sub-agent tasks.
inline constexpr const char* kLightModel = "claude-haiku-4-5";

// Return baseCmd with a `--model` flag appended for token routing.
//   modelOverride non-empty -> append it (explicit wins).
//   else light              -> append the cheap tier.
//   else                    -> unchanged (inherit the CLI default).
// If baseCmd already carries a `--model`, leave it untouched unless an
// explicit override is given (override still appends; last flag wins in the CLI).
inline std::string applyAgentModel(const std::string& baseCmd, bool light,
                                   const std::string& modelOverride) {
    if (!modelOverride.empty()) return baseCmd + " --model " + modelOverride;
    if (light && baseCmd.find("--model") == std::string::npos)
        return baseCmd + " --model " + kLightModel;
    return baseCmd;
}

} // namespace icmg::cli
