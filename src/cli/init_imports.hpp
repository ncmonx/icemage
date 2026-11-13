#pragma once
// Background import chain run by `icmg init` (CLAUDE.md / plan / skill index).
//
// Issue #279: `icmg update --apply` refreshes project hooks via
// `init --install-hooks --force`, and init used to always fork
// `icmg claudemd import --slim`, silently rewriting a user-authored,
// team-shared CLAUDE.md into a pointer stub. Now:
//   - init imports CLAUDE.md into the graph WITHOUT slimming by default;
//     slimming is an explicit opt-in (`init --slim` or `claudemd import --slim`).
//   - `init --no-import` skips the background imports entirely; update's hook
//     refresh always passes it (an update must never touch user docs).
#include <algorithm>
#include <string>
#include <vector>

namespace icmg::cli {

// Flags update_cmd appends to `<icmg> init` when refreshing project hooks.
inline constexpr const char* kUpdateInitRefreshFlags =
    "--install-hooks --force --no-agents --no-embedder --no-scan --no-import";

inline bool initHasFlag(const std::vector<std::string>& args, const char* f) {
    return std::find(args.begin(), args.end(), f) != args.end();
}

inline bool initImportsEnabled(const std::vector<std::string>& args) {
    return !initHasFlag(args, "--no-import");
}

inline bool initSlimRequested(const std::vector<std::string>& args) {
    return initHasFlag(args, "--slim");
}

// `sep` is the shell sequencing token: " & " (cmd.exe) or "; " (sh).
inline std::string initImportChain(bool slim, const std::string& sep) {
    std::string c = "icmg claudemd import";
    if (slim) c += " --slim";
    c += sep + "icmg plan import" + sep + "icmg skill index";
    return c;
}

}  // namespace icmg::cli
