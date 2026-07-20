#pragma once
// Multi-tool wiring for `icmg init --all-tools` (gap G6 vs graphify's
// `install --strict`). Detects which AI coding CLIs are present on the machine
// and drops a small "routing rule" file at each tool's known config location
// so the agent is pointed at icmg. Claude Code gets the full native hook setup
// elsewhere in init; this covers the other hosts with an instructional file
// (real config, not just a printed hint).
//
// Everything here is pure/deterministic except isToolPresent (a filesystem
// probe) and writeRouting (writes one file). Header-only for easy unit testing.

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace icmg::cli::toolwiring {

struct ToolTarget {
    std::string name;          // "cursor", "windsurf", ...
    std::string configRelPath; // path (project-rel if projectLevel, else home-rel)
    bool projectLevel = true;  // true: under project root; false: under home dir
    // A marker whose existence means the tool is installed/used here. Relative
    // to project root (projectLevel) or home (!projectLevel). Empty => use the
    // parent dir of configRelPath as the marker.
    std::string detectRelPath;
};

// The host CLIs icmg can wire, with their per-tool config location.
inline std::vector<ToolTarget> knownTools() {
    return {
        // name        configRelPath                              project  detect
        {"cursor",    ".cursor/rules/icmg.mdc",                    true,  ".cursor"},
        {"windsurf",  ".windsurfrules",                           true,  ".windsurfrules"},
        {"zed",       ".zed/icmg.md",                             true,  ".zed"},
        {"codex",     ".codex/icmg.md",                           true,  ".codex"},
        {"copilot",   ".github/copilot-instructions.md",         true,  ".github"},
        {"opencode",  ".opencode/icmg.md",                        true,  ".opencode"},
        {"gemini",    ".gemini/icmg.md",                          true,  ".gemini"},
        {"amp",       ".amp/icmg.md",                             true,  ".amp"},
    };
}

// Routing text written for a given tool. Deterministic; empty for unknown tool.
inline std::string routingContent(const std::string& tool) {
    bool known = false;
    for (const auto& t : knownTools()) if (t.name == tool) { known = true; break; }
    if (!known) return {};

    // Shared body: the same icmg-first guidance, framed per tool. Kept short so
    // it fits any host's rule-file budget.
    std::string s;
    s += "# icmg routing (auto-added by `icmg init --all-tools`)\n\n";
    s += "This project uses **icmg** for token-efficient code navigation and\n";
    s += "persistent memory. Prefer icmg over raw shell/file tools:\n\n";
    s += "- Read a large file  -> `icmg context <file>` (graph + symbols + memory)\n";
    s += "- Find a symbol      -> `icmg graph symbol <Name>`\n";
    s += "- Search code        -> `icmg run grep ...` (auto-filtered)\n";
    s += "- Recall a decision  -> `icmg recall \"<query>\"`\n";
    s += "- Start a task       -> `icmg pack \"<task>\"` (context bundle)\n";
    s += "- Run a noisy cmd    -> `icmg run <cmd>` (Tkil filter, 60-90% smaller)\n\n";
    s += "See `AGENTS.md` for the full routing table.\n";
    return s;
}

// Resolve the detection marker path for a tool.
inline std::filesystem::path markerPath(const ToolTarget& t,
                                        const std::filesystem::path& projectRoot,
                                        const std::filesystem::path& homeDir) {
    std::string rel = t.detectRelPath.empty()
        ? std::filesystem::path(t.configRelPath).parent_path().string()
        : t.detectRelPath;
    return (t.projectLevel ? projectRoot : homeDir) / rel;
}

// True if the tool appears installed/used here (its config dir/file exists).
inline bool isToolPresent(const ToolTarget& t,
                          const std::filesystem::path& projectRoot,
                          const std::filesystem::path& homeDir) {
    std::error_code ec;
    return std::filesystem::exists(markerPath(t, projectRoot, homeDir), ec);
}

// Write the routing file for a tool; creates parent dirs. Returns the path
// written (empty on failure or unknown tool).
inline std::filesystem::path writeRouting(const ToolTarget& t,
                                          const std::filesystem::path& projectRoot,
                                          const std::filesystem::path& homeDir) {
    std::string content = routingContent(t.name);
    if (content.empty()) return {};
    std::filesystem::path dst =
        (t.projectLevel ? projectRoot : homeDir) / t.configRelPath;
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return {};
    out << content;
    out.close();
    return out ? dst : std::filesystem::path{};
}

} // namespace icmg::cli::toolwiring
