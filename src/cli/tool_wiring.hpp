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
    // Optional second detection marker (e.g. windsurf legacy .windsurfrules).
    std::string detectRelPath2;
    // true: the config file is SHARED with the user (AGENTS.md, GEMINI.md,
    // copilot-instructions.md, junie guidelines). Write a replaceable managed
    // block instead of truncating; no-op if init's full icmg block is present.
    bool managedBlock = false;
};

// Markers for the managed tool-wiring block. Distinct from init's main
// "<!-- icmg:start -->" AGENTS.md block so the two never fight.
inline constexpr const char* kWiringBlockStart = "<!-- icmg-wiring:start -->";
inline constexpr const char* kWiringBlockEnd   = "<!-- icmg-wiring:end -->";
inline constexpr const char* kInitBlockStart   = "<!-- icmg:start -->";

// The host CLIs icmg can wire, with their per-tool config location.
// Audited 2026-10-26 against each host's docs: Gemini CLI reads GEMINI.md
// (project root), Codex/Zed/Amp/opencode read AGENTS.md, Windsurf moved to
// .windsurf/rules/ (.windsurfrules deprecated), Cline reads .clinerules/,
// Antigravity reads .agents/rules/*.md (frontmatter) + AGENTS.md/GEMINI.md.
inline std::vector<ToolTarget> knownTools() {
    return {
        // name          configRelPath                      proj  detect         detect2            managed
        {"cursor",      ".cursor/rules/icmg.mdc",           true, ".cursor",      "",               false},
        {"windsurf",    ".windsurf/rules/icmg.md",          true, ".windsurf",    ".windsurfrules", false},
        {"zed",         "AGENTS.md",                        true, ".zed",         ".rules",         true},
        {"codex",       "AGENTS.md",                        true, ".codex",       "",               true},
        {"copilot",     ".github/copilot-instructions.md",  true, ".github",      "",               true},
        {"opencode",    "AGENTS.md",                        true, ".opencode",    "opencode.json",  true},
        {"gemini",      "GEMINI.md",                        true, ".gemini",      "GEMINI.md",      true},
        {"amp",         "AGENTS.md",                        true, ".amp",         "",               true},
        {"cline",       ".clinerules/icmg.md",              true, ".clinerules",  "",               false},
        {"antigravity", ".agents/rules/icmg.md",            true, ".agents",      ".agent",         false},
        {"roo",         ".roo/rules/icmg.md",               true, ".roo",         "",               false},
        {"kilo",        ".kilocode/rules/icmg.md",          true, ".kilocode",    "",               false},
        {"junie",       ".junie/guidelines.md",             true, ".junie",       "",               true},
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
    // Antigravity rules require YAML frontmatter with a trigger. Use
    // model_decision: progressive disclosure (path+description injected,
    // body read on demand) -- cheapest in tokens.
    if (tool == "antigravity") {
        s += "---\n";
        s += "trigger: model_decision\n";
        s += "description: \"Token-efficient code navigation and persistent "
             "memory via icmg. Apply when reading files, searching code, "
             "tracing impact, or recalling project decisions.\"\n";
        s += "---\n\n";
    }
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
    if (std::filesystem::exists(markerPath(t, projectRoot, homeDir), ec)) return true;
    if (!t.detectRelPath2.empty()) {
        auto alt = (t.projectLevel ? projectRoot : homeDir) / t.detectRelPath2;
        if (std::filesystem::exists(alt, ec)) return true;
    }
    return false;
}

// Replace (or append) the managed wiring block inside an existing shared file.
// Returns the new full file body.
inline std::string upsertWiringBlock(const std::string& existing,
                                     const std::string& content) {
    std::string block = std::string(kWiringBlockStart) + "\n" + content +
                        kWiringBlockEnd + "\n";
    size_t b = existing.find(kWiringBlockStart);
    size_t e = existing.find(kWiringBlockEnd);
    if (b != std::string::npos && e != std::string::npos && e >= b) {
        std::string out = existing.substr(0, b);
        out += block;
        out += existing.substr(e + std::string(kWiringBlockEnd).size());
        // drop a single leading newline left behind by the old block tail
        return out;
    }
    std::string out = existing;
    if (!out.empty() && out.back() != '\n') out += "\n";
    if (!out.empty()) out += "\n";
    out += block;
    return out;
}

// Write the routing file for a tool; creates parent dirs. Returns the path
// written (empty on failure or unknown tool). Managed targets (shared files
// like AGENTS.md / GEMINI.md) get an idempotent marker block instead of a
// truncating write, and are left untouched when init's full icmg block is
// already present (it already routes the agent to icmg).
inline std::filesystem::path writeRouting(const ToolTarget& t,
                                          const std::filesystem::path& projectRoot,
                                          const std::filesystem::path& homeDir) {
    std::string content = routingContent(t.name);
    if (content.empty()) return {};
    std::filesystem::path dst =
        (t.projectLevel ? projectRoot : homeDir) / t.configRelPath;
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);

    if (t.managedBlock) {
        std::string existing;
        if (std::filesystem::exists(dst, ec)) {
            std::ifstream in(dst, std::ios::binary);
            if (!in) return {};
            existing.assign((std::istreambuf_iterator<char>(in)), {});
        }
        // init's full routing block already points the agent at icmg -> no-op.
        if (existing.find(kInitBlockStart) != std::string::npos) return dst;
        std::string merged = upsertWiringBlock(existing, content);
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        if (!out) return {};
        out << merged;
        out.close();
        return out ? dst : std::filesystem::path{};
    }

    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return {};
    out << content;
    out.close();
    return out ? dst : std::filesystem::path{};
}

} // namespace icmg::cli::toolwiring
