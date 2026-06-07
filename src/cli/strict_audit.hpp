#pragma once
// `icmg strict audit` — read-only hard-deny enforcement for token-frugal audits.
//
// Why a separate mode (not the global default):
//   Hard-denying native Read breaks Edit — Claude Code requires Read-before-Edit,
//   so a global Read deny would make every edit fail (logged failure, v0.33.4).
//   An AUDIT session is read-only, so denying full-file Read is safe there and
//   forces `icmg context` (~80% token cut). This mode is opt-in / per-session,
//   never the global default.
//
// Covers two leak vectors the normal hooks miss:
//   1. native Read (bash hook only sees `cat`, not the Read tool) -> deny full reads
//   2. browser-automation MCP (puppeteer/playwright) whose DOM/screenshot payloads
//      bypass the bash filter entirely -> deny pre-charge
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace icmg::cli::strict_audit {

// Audit mode is active when env ICMG_STRICT_AUDIT=1 OR the persistent flag
// file exists (written by `icmg strict audit on`). Env wins for per-session use.
inline bool flagActive(const std::filesystem::path& flagFile, const char* envVal) {
    if (envVal && std::string(envVal) == "1") return true;
    std::error_code ec;
    return std::filesystem::exists(flagFile, ec);
}

// Native full-file Read should be denied in audit mode. A targeted slice
// (offset/limit already set) is token-frugal -> let it through.
inline bool readShouldDeny(bool auditOn, bool targeted) {
    return auditOn && !targeted;
}

// Browser-automation MCP tools emit large DOM/screenshot payloads that the
// bash-rewrite filter never sees (MCP calls don't go through Bash).
inline bool isHeavyBrowserMcp(const std::string& toolName) {
    std::string n = toolName;
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const char* kNeedles[] = {
        "puppeteer", "playwright", "browser", "chrome",
        "screenshot", "navigate", "page_", "dom_"
    };
    for (const char* needle : kNeedles)
        if (n.find(needle) != std::string::npos) return true;
    return false;
}

inline bool mcpShouldDeny(bool auditOn, const std::string& toolName) {
    return auditOn && isHeavyBrowserMcp(toolName);
}

} // namespace icmg::cli::strict_audit
