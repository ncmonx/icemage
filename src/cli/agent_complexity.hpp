// Auto-routing for `icmg agent`: classify a task as lightweight (mechanical,
// bounded) so it can run on a cheap model by default — no manual --light.
//
// Conservative by design: only return true when the task is CLEARLY mechanical.
// Anything ambiguous or carrying a "heavy" signal (design/debug/refactor/...)
// stays on the full model, because a wrong cheap route wastes the whole run.
#pragma once

#include <string>
#include <array>
#include <cctype>

namespace icmg::cli {

inline bool isLightweightTask(const std::string& task) {
    std::string t(task);
    for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto has = [&t](const char* w) { return t.find(w) != std::string::npos; };

    // Heavy signals: real engineering judgment — never auto-cheap these.
    static const std::array<const char*, 12> kHeavy = {{
        "design", "architect", "refactor", "debug", "investigate",
        "optimize", "implement", "analyze", "review", "plan ",
        "fix ", "why "
    }};
    for (const char* w : kHeavy) if (has(w)) return false;

    // Mechanical signals: clearly bounded, safe to route cheap.
    static const std::array<const char*, 12> kLight = {{
        "create a file", "create file", "rename", "delete", "move ",
        "copy ", "append", "add a line", "list ", "print ", "echo ", "typo"
    }};
    for (const char* w : kLight) if (has(w)) return true;

    return false; // unknown -> conservative (full model)
}

} // namespace icmg::cli
