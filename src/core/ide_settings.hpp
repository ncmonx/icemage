#pragma once
// Editor workspace-settings helpers (.vscode/settings.json — VSCode & Cursor).
//
// VSCode/Cursor terminals detect file paths in output and validate them (stat)
// to render clickable links. When tool output contains a drive-letter token
// like "b:/..." and that drive is a dead/disconnected mapping (common on
// Windows: a leftover B: net-use), the validation probe raises a modal
//   "B:/  The system cannot find the drive specified."
// popup — repeatedly, on every `icmg recall`/`context` that prints such a
// token. It is not an icmg bug (output is clean text), but icmg can spare the
// user the annoyance by turning terminal file-link detection off in the
// workspace. `enableFileLinks: "off"` stops the path probe (URL links still
// work). See known-issue #33001.
//
// Pure (operates on parsed json) so it is unit-testable in isolation.
#include <nlohmann/json.hpp>

namespace icmg::core {

// Set VSCode/Cursor `terminal.integrated.enableFileLinks` to "off" so the
// terminal stops validating drive-letter path tokens (the B:/ popup source).
// Never clobbers an existing user value. Returns true if it set the key.
inline bool ensureTerminalFileLinksOff(nlohmann::json& cfg) {
    const char* key = "terminal.integrated.enableFileLinks";
    if (cfg.contains(key) && !cfg[key].is_null()) return false;
    cfg[key] = "off";
    return true;
}

}  // namespace icmg::core
