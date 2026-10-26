#pragma once
// MCP auto-wiring for `icmg init --all-tools` (2026-10-26 host-compat audit).
// icmg ships a 42-tool MCP stdio server (`icmg --mcp-server`) but init never
// registered it with any host. This header writes/merges the icmg server
// entry into each host's MCP config, preserving every user key and never
// clobbering an existing (possibly customized) "icmg" entry.
//
// Per-host config locations (verified against host docs, Oct 2026):
//   claude-code  .mcp.json                 (project)        mcpServers
//   cursor       .cursor/mcp.json          (project)        mcpServers
//   vscode       .vscode/mcp.json          (project)        servers (+type stdio)
//   gemini       .gemini/settings.json     (project)        mcpServers
//   windsurf     .codeium/windsurf/mcp_config.json (home)   mcpServers
//
// Header-only + pure except writeMcpConfig (one file write) for easy testing.

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <nlohmann/json.hpp>

namespace icmg::cli::mcpwiring {

struct McpTarget {
    std::string name;          // "claude-code", "cursor", ...
    std::string configRelPath; // config file (project-rel or home-rel)
    bool projectLevel = true;  // true: under project root; false: under home
    std::string detectRelPath; // marker dir/file meaning the host is used here
    std::string serversKey = "mcpServers"; // top-level key holding servers
    bool stdioType = false;    // true: entry needs "type":"stdio" (VS Code)
};

inline std::vector<McpTarget> knownMcpTargets() {
    return {
        {"claude-code", ".mcp.json",                         true,  ".claude",  "mcpServers", false},
        {"cursor",      ".cursor/mcp.json",                  true,  ".cursor",  "mcpServers", false},
        {"vscode",      ".vscode/mcp.json",                  true,  ".vscode",  "servers",    true},
        {"gemini",      ".gemini/settings.json",             true,  ".gemini",  "mcpServers", false},
        {"windsurf",    ".codeium/windsurf/mcp_config.json", false, ".codeium/windsurf", "mcpServers", false},
    };
}

// The icmg server entry for a given target. Deterministic.
inline nlohmann::json serverEntry(const McpTarget& t) {
    nlohmann::json e;
    if (t.stdioType) e["type"] = "stdio";
    e["command"] = "icmg";
    e["args"]    = nlohmann::json::array({"--mcp-server"});
    return e;
}

// True if the host appears installed/used here.
inline bool isHostPresent(const McpTarget& t,
                          const std::filesystem::path& projectRoot,
                          const std::filesystem::path& homeDir) {
    std::error_code ec;
    auto base = t.projectLevel ? projectRoot : homeDir;
    if (std::filesystem::exists(base / t.detectRelPath, ec)) return true;
    return std::filesystem::exists(base / t.configRelPath, ec);
}

// Merge the icmg entry into the host's MCP config. Creates the file if
// missing; preserves all existing keys; skips when an "icmg" entry already
// exists (user may have customized it). Returns the config path (empty on
// failure).
inline std::filesystem::path writeMcpConfig(const McpTarget& t,
                                            const std::filesystem::path& projectRoot,
                                            const std::filesystem::path& homeDir) {
    namespace fs = std::filesystem;
    fs::path dst = (t.projectLevel ? projectRoot : homeDir) / t.configRelPath;
    std::error_code ec;

    nlohmann::json cfg = nlohmann::json::object();
    if (fs::exists(dst, ec)) {
        try {
            std::ifstream in(dst);
            in >> cfg;
            if (!cfg.is_object()) cfg = nlohmann::json::object();
        } catch (...) {
            // Unparseable user config: do NOT risk destroying it.
            return {};
        }
    }

    if (!cfg.contains(t.serversKey) || !cfg[t.serversKey].is_object())
        cfg[t.serversKey] = nlohmann::json::object();
    if (cfg[t.serversKey].contains("icmg")) return dst;   // never clobber

    cfg[t.serversKey]["icmg"] = serverEntry(t);

    fs::create_directories(dst.parent_path(), ec);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return {};
    out << cfg.dump(2) << "\n";
    out.close();
    return out ? dst : fs::path{};
}

} // namespace icmg::cli::mcpwiring
