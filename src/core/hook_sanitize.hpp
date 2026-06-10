#pragma once
// Hook settings.json sanitizers.
//
// Old `icmg init` installed a python PreCompact hook
// (`python3 ~/.claude/hooks/icmg-precompact-snapshot.py`). Core icmg is now
// Python-free and the native `icmg hook precompact` replaces it, but the stale
// settings.json ENTRY survived re-init (the existing sanitizer only matched the
// inline `python3 -c` SessionStart form, not this script-file form). On a host
// without python3 it prints `python3: command not found` on every compaction.
// This drops any hook command referencing that dead script across all events.
//
// Pure (operates on a parsed json) so it is unit-testable in isolation.
#include <nlohmann/json.hpp>
#include <string>

namespace icmg::core {

// Remove obsolete hook commands that reference the dead python precompact
// snapshot script. Prunes hook entries whose `hooks` list becomes empty.
// Returns the number of hook commands removed (0 = nothing to do).
inline int removeStaleSnapshotHooks(nlohmann::json& cfg) {
    int removed = 0;
    if (!cfg.contains("hooks") || !cfg["hooks"].is_object()) return 0;
    const std::string dead = "icmg-precompact-snapshot.py";
    for (auto& ev : cfg["hooks"].items()) {
        auto& arr = ev.value();
        if (!arr.is_array()) continue;
        for (auto entryIt = arr.begin(); entryIt != arr.end(); ) {
            if (entryIt->contains("hooks") && (*entryIt)["hooks"].is_array()) {
                auto& hooks = (*entryIt)["hooks"];
                for (auto hIt = hooks.begin(); hIt != hooks.end(); ) {
                    if (hIt->contains("command") && (*hIt)["command"].is_string()
                        && (*hIt)["command"].get<std::string>().find(dead) != std::string::npos) {
                        hIt = hooks.erase(hIt);
                        ++removed;
                    } else {
                        ++hIt;
                    }
                }
                if (hooks.empty()) { entryIt = arr.erase(entryIt); continue; }
            }
            ++entryIt;
        }
    }
    return removed;
}

// Ensure Claude Code's statusLine runs `command` (default makes icmg's per-model
// context budget visible every turn). Won't clobber a user's existing statusLine.
// Returns true if it added one.
inline bool ensureStatusLine(nlohmann::json& cfg, const std::string& command) {
    if (cfg.contains("statusLine") && !cfg["statusLine"].is_null()) return false;
    cfg["statusLine"] = { {"type", "command"}, {"command", command} };
    return true;
}

}  // namespace icmg::core
