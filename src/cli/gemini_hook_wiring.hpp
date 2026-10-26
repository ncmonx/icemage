#pragma once
// Gemini CLI hook parity (2026-10-26 host-compat audit).
//
// Gemini CLI supports hooks whose I/O schema matches Claude Code's almost
// 1:1 (settings.json "hooks" object; SessionStart emits
// {"hookSpecificOutput":{"additionalContext":...}} on stdout; exit 2 blocks).
// icmg already ships `icmg session-inject`, which emits exactly that JSON,
// so the SessionStart wiring is a direct reuse -- Gemini sessions get the
// same icmg context rehydration Claude Code sessions do.
//
// Scope: SessionStart (advisory, never blocks startup per the Gemini hooks
// reference) + BeforeTool for read_file/read_many_files (tool-name parity:
// Gemini's read_file == Claude's Read; `icmg hook pretooluse-read --format
// gemini` translates both the input field names and the output schema).

#include <string>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <nlohmann/json.hpp>

namespace icmg::cli::geminihooks {

inline constexpr const char* kSessionInjectCmd = "icmg session-inject";
inline constexpr const char* kPreToolReadCmd =
    "icmg hook pretooluse-read --format gemini";
inline constexpr const char* kReadToolMatcher = "read_file|read_many_files";

// Translate a Claude-schema PreToolUse hook output into the Gemini CLI
// BeforeTool schema. Pure. Mapping (per Gemini hooks reference, Oct 2026):
//   permissionDecision deny  -> decision:"deny" + reason
//   updatedInput             -> hookSpecificOutput.tool_input (merge+override;
//                               file_path renamed absolute_path -- Gemini's
//                               read_file argument name)
//   additionalContext        -> systemMessage (BeforeTool has no
//                               additionalContext channel)
inline nlohmann::json toGeminiPre(const nlohmann::json& claude) {
    nlohmann::json g = nlohmann::json::object();
    if (!claude.is_object() || !claude.contains("hookSpecificOutput")) return g;
    const auto& h = claude["hookSpecificOutput"];

    std::string decision = h.value("permissionDecision", std::string{});
    if (decision == "deny" || decision == "block") {
        g["decision"] = "deny";
        g["reason"]   = h.value("permissionDecisionReason", std::string{});
        return g;
    }

    if (h.contains("updatedInput") && h["updatedInput"].is_object()) {
        nlohmann::json ti = nlohmann::json::object();
        for (auto it = h["updatedInput"].begin(); it != h["updatedInput"].end(); ++it) {
            if (it.key() == "file_path") ti["absolute_path"] = it.value();
            else                          ti[it.key()] = it.value();
        }
        // merge semantics: emit ONLY fields we actually override; echoing the
        // unchanged path back is harmless (it merges to the same value)
        g["hookSpecificOutput"]["tool_input"] = ti;
    }

    std::string ctx = h.value("additionalContext", std::string{});
    if (!ctx.empty()) g["systemMessage"] = ctx;
    return g;
}

// Merge the icmg SessionStart + BeforeTool(read) hooks into a Gemini CLI
// settings.json. Idempotent (detects existing icmg commands) and preserves
// every user key. Returns true when the file now contains the hooks.
inline bool wireGeminiHooks(const std::filesystem::path& settingsPath) {
    namespace fs = std::filesystem;
    std::error_code ec;

    nlohmann::json cfg = nlohmann::json::object();
    if (fs::exists(settingsPath, ec)) {
        try {
            std::ifstream in(settingsPath);
            in >> cfg;
            if (!cfg.is_object()) cfg = nlohmann::json::object();
        } catch (...) {
            return false;   // never risk destroying an unparseable user config
        }
    }

    if (!cfg.contains("hooks") || !cfg["hooks"].is_object())
        cfg["hooks"] = nlohmann::json::object();
    auto& hooks = cfg["hooks"];

    auto hasCmd = [](const nlohmann::json& arr, const char* needle) {
        if (!arr.is_array()) return false;
        for (const auto& def : arr) {
            if (!def.is_object() || !def.contains("hooks")) continue;
            for (const auto& h : def["hooks"]) {
                if (h.is_object() && h.value("command", std::string{})
                                       .find(needle) != std::string::npos)
                    return true;
            }
        }
        return false;
    };

    if (!hooks.contains("SessionStart") || !hooks["SessionStart"].is_array())
        hooks["SessionStart"] = nlohmann::json::array();
    if (!hasCmd(hooks["SessionStart"], kSessionInjectCmd)) {
        hooks["SessionStart"].push_back({
            {"hooks", nlohmann::json::array({{
                {"type",    "command"},
                {"name",    "icmg-session-context"},
                {"timeout", 10000},
                {"command", kSessionInjectCmd}
            }})}
        });
    }

    if (!hooks.contains("BeforeTool") || !hooks["BeforeTool"].is_array())
        hooks["BeforeTool"] = nlohmann::json::array();
    if (!hasCmd(hooks["BeforeTool"], "pretooluse-read")) {
        hooks["BeforeTool"].push_back({
            {"matcher", kReadToolMatcher},
            {"hooks", nlohmann::json::array({{
                {"type",    "command"},
                {"name",    "icmg-read-routing"},
                {"timeout", 5000},
                {"command", kPreToolReadCmd}
            }})}
        });
    }

    fs::create_directories(settingsPath.parent_path(), ec);
    std::ofstream out(settingsPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << cfg.dump(2) << "\n";
    out.close();
    return (bool)out;
}

} // namespace icmg::cli::geminihooks
