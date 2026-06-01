// v1.75.1: schema-valid PreCompact hook output.
//
// Claude Code's PreCompact event does NOT accept `hookSpecificOutput`
// (with `additionalContext` / `hookEventName`) — only Pre/PostToolUse,
// PostToolBatch, and UserPromptSubmit do. Emitting that shape made every
// `icmg hook precompact` invocation fail schema validation
// ("(root): Invalid input"), so the output was dropped on every compaction.
//
// The real PreCompact work (snapshot, distill, transcript record, snippet
// extraction) happens as side-effects in runPreCompactHook(). Model
// re-anchoring of rules + pinned decisions is delivered by the working
// SessionStart:compact hook (icmg-postcompact-memory.sh). So PreCompact only
// needs to emit a schema-valid no-op.
#pragma once

#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace icmg {
namespace core {
namespace hooks {

// The set of top-level keys Claude Code accepts from a PreCompact hook.
inline const std::set<std::string>& preCompactAllowedKeys() {
    static const std::set<std::string> allowed = {
        "continue", "suppressOutput", "stopReason",
        "decision", "reason", "systemMessage"};
    return allowed;
}

// Schema-valid PreCompact output: a no-op that suppresses stdout noise.
inline std::string preCompactOutputJson() {
    nlohmann::json out;
    out["suppressOutput"] = true;
    return out.dump();
}

// True iff `s` parses to an object whose every top-level key is allowed for
// the PreCompact event (used by tests + as a defensive guard).
inline bool isValidPreCompactOutput(const std::string& s) {
    if (s.empty()) return true;  // emitting nothing is always valid
    try {
        auto j = nlohmann::json::parse(s);
        if (!j.is_object()) return false;
        const auto& allowed = preCompactAllowedKeys();
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (allowed.find(it.key()) == allowed.end()) return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace hooks
}  // namespace core
}  // namespace icmg
