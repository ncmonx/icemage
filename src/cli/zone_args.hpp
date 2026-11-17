// Argument validation for `icmg zone add <name> <glob>` (#283).
//
// A name or glob starting with "--" is a mistyped flag, e.g. the old AGENTS.md
// template `icmg zone add <path> --zone <subsystem>`. Accepting it created junk
// zones whose glob matched nothing. Reject before any DB write. Pure +
// header-only so it is trivially testable.
#pragma once

#include <string>
#include <vector>

namespace icmg::cli {

inline constexpr const char* kZoneAddUsage = "icmg zone add <name> <glob>";

// Empty string when args are valid, else an ASCII error message.
inline std::string validateZoneAddArgs(const std::vector<std::string>& args) {
    if (args.size() < 2)
        return std::string("requires <name> <glob> (usage: ") + kZoneAddUsage + ")";
    for (size_t i = 0; i < 2; ++i) {
        const std::string& a = args[i];
        if (a.empty())
            return std::string(i == 0 ? "<name>" : "<glob>") + " must not be empty";
        if (a.rfind("--", 0) == 0)
            return "'" + a + "' looks like a flag; zone add has no flags. Usage: "
                   + kZoneAddUsage + " (e.g. icmg zone add auth 'src/auth/**')";
    }
    if (args.size() > 2)
        return "too many arguments (" + std::to_string(args.size())
               + "); usage: " + kZoneAddUsage + " -- quote globs containing spaces";
    return {};
}

} // namespace icmg::cli
