// tests/cli/test_host_wiring.cpp
//
// TDD tests for host MCP auto-wiring (src/cli/mcp_wiring.hpp) and Gemini CLI
// hook parity (src/cli/gemini_hook_wiring.hpp) -- 2026-10-26 host-compat audit.
// Pure header helpers; filesystem exercised only against temp dirs.

#include "../test_main.hpp"
#include "../../src/cli/mcp_wiring.hpp"
#include "../../src/cli/gemini_hook_wiring.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using namespace icmg::cli::mcpwiring;
using nlohmann::json;

static json readJson(const fs::path& p) {
    std::ifstream in(p);
    json j; in >> j;
    return j;
}

TEST("mcp-wiring: known targets cover claude-code/cursor/vscode/gemini/windsurf") {
    auto v = knownMcpTargets();
    ASSERT_TRUE(v.size() >= 5);
    bool cc = false, cu = false, vs = false, ge = false, ws = false;
    for (const auto& t : v) {
        if (t.name == "claude-code") cc = true;
        if (t.name == "cursor")      cu = true;
        if (t.name == "vscode")      vs = true;
        if (t.name == "gemini")      ge = true;
        if (t.name == "windsurf")    ws = true;
    }
    ASSERT_TRUE(cc); ASSERT_TRUE(cu); ASSERT_TRUE(vs); ASSERT_TRUE(ge); ASSERT_TRUE(ws);
}

static McpTarget getTarget(const std::string& n) {
    for (const auto& t : knownMcpTargets()) if (t.name == n) return t;
    return {};
}

TEST("mcp-wiring: fresh config gets icmg stdio server entry") {
    fs::path d = fs::temp_directory_path() / "icmg_mcpwire_fresh";
    std::error_code ec; fs::remove_all(d, ec);
    fs::create_directories(d, ec);

    McpTarget cc = getTarget("claude-code");
    fs::path w = writeMcpConfig(cc, d, d);
    ASSERT_TRUE(!w.empty());

    json j = readJson(w);
    ASSERT_TRUE(j.contains("mcpServers"));
    ASSERT_TRUE(j["mcpServers"].contains("icmg"));
    ASSERT_TRUE(j["mcpServers"]["icmg"]["command"] == "icmg");
    ASSERT_TRUE(j["mcpServers"]["icmg"]["args"][0] == "--mcp-server");
    fs::remove_all(d, ec);
}

TEST("mcp-wiring: merge preserves existing keys and never clobbers user icmg entry") {
    fs::path d = fs::temp_directory_path() / "icmg_mcpwire_merge";
    std::error_code ec; fs::remove_all(d, ec);
    fs::create_directories(d, ec);

    {
        std::ofstream f((d / ".mcp.json").string());
        f << R"({"mcpServers":{"other":{"command":"x"}},"foo":1})";
    }
    McpTarget cc = getTarget("claude-code");
    fs::path w = writeMcpConfig(cc, d, d);
    ASSERT_TRUE(!w.empty());
    json j = readJson(w);
    ASSERT_TRUE(j["foo"] == 1);
    ASSERT_TRUE(j["mcpServers"]["other"]["command"] == "x");
    ASSERT_TRUE(j["mcpServers"].contains("icmg"));

    // user customized the icmg entry -> re-wiring must NOT overwrite it
    j["mcpServers"]["icmg"]["command"] = "custom-icmg";
    { std::ofstream f((d / ".mcp.json").string()); f << j.dump(2); }
    writeMcpConfig(cc, d, d);
    json j2 = readJson(d / ".mcp.json");
    ASSERT_TRUE(j2["mcpServers"]["icmg"]["command"] == "custom-icmg");
    fs::remove_all(d, ec);
}

TEST("mcp-wiring: vscode uses 'servers' key with stdio type") {
    fs::path d = fs::temp_directory_path() / "icmg_mcpwire_vscode";
    std::error_code ec; fs::remove_all(d, ec);
    fs::create_directories(d, ec);

    McpTarget vs = getTarget("vscode");
    ASSERT_TRUE(vs.serversKey == "servers");
    fs::path w = writeMcpConfig(vs, d, d);
    ASSERT_TRUE(!w.empty());
    json j = readJson(w);
    ASSERT_TRUE(j["servers"]["icmg"]["type"] == "stdio");
    ASSERT_TRUE(j["servers"]["icmg"]["command"] == "icmg");
    fs::remove_all(d, ec);
}

TEST("mcp-wiring: gemini target merges into .gemini/settings.json") {
    fs::path d = fs::temp_directory_path() / "icmg_mcpwire_gem";
    std::error_code ec; fs::remove_all(d, ec);
    fs::create_directories(d / ".gemini", ec);
    {
        std::ofstream f((d / ".gemini" / "settings.json").string());
        f << R"({"theme":"dark"})";
    }
    McpTarget ge = getTarget("gemini");
    fs::path w = writeMcpConfig(ge, d, d);
    ASSERT_TRUE(!w.empty());
    json j = readJson(w);
    ASSERT_TRUE(j["theme"] == "dark");
    ASSERT_TRUE(j["mcpServers"]["icmg"]["command"] == "icmg");
    fs::remove_all(d, ec);
}

// --- Gemini CLI hooks parity -------------------------------------------------

TEST("gemini-hooks: fresh settings.json gets SessionStart icmg hook") {
    fs::path d = fs::temp_directory_path() / "icmg_gemhook_fresh";
    std::error_code ec; fs::remove_all(d, ec);
    fs::create_directories(d / ".gemini", ec);

    fs::path sp = d / ".gemini" / "settings.json";
    ASSERT_TRUE(icmg::cli::geminihooks::wireGeminiHooks(sp));

    json j = readJson(sp);
    ASSERT_TRUE(j.contains("hooks"));
    ASSERT_TRUE(j["hooks"].contains("SessionStart"));
    std::string cmd = j["hooks"]["SessionStart"][0]["hooks"][0]["command"];
    ASSERT_CONTAINS(cmd, "icmg session-inject");
    fs::remove_all(d, ec);
}

TEST("gemini-hooks: idempotent and preserves user settings") {
    fs::path d = fs::temp_directory_path() / "icmg_gemhook_idem";
    std::error_code ec; fs::remove_all(d, ec);
    fs::create_directories(d / ".gemini", ec);

    fs::path sp = d / ".gemini" / "settings.json";
    { std::ofstream f(sp.string()); f << R"({"theme":"light"})"; }

    ASSERT_TRUE(icmg::cli::geminihooks::wireGeminiHooks(sp));
    ASSERT_TRUE(icmg::cli::geminihooks::wireGeminiHooks(sp));   // second run: no dup

    json j = readJson(sp);
    ASSERT_TRUE(j["theme"] == "light");
    ASSERT_TRUE(j["hooks"]["SessionStart"].size() == 1);
    ASSERT_TRUE(j["hooks"]["BeforeTool"].size() == 1);
    fs::remove_all(d, ec);
}

TEST("gemini-hooks: BeforeTool wires read_file matcher to icmg pretool read") {
    fs::path d = fs::temp_directory_path() / "icmg_gemhook_bt";
    std::error_code ec; fs::remove_all(d, ec);
    fs::create_directories(d / ".gemini", ec);

    fs::path sp = d / ".gemini" / "settings.json";
    ASSERT_TRUE(icmg::cli::geminihooks::wireGeminiHooks(sp));
    json j = readJson(sp);
    ASSERT_TRUE(j["hooks"].contains("BeforeTool"));
    auto& def = j["hooks"]["BeforeTool"][0];
    ASSERT_TRUE(def["matcher"] == "read_file|read_many_files");
    std::string cmd = def["hooks"][0]["command"];
    ASSERT_CONTAINS(cmd, "icmg hook pretooluse-read");
    ASSERT_CONTAINS(cmd, "--format gemini");
    fs::remove_all(d, ec);
}

// --- Claude-schema -> Gemini-schema output translation (pure) ---------------

TEST("gemini-hooks: toGeminiPre maps deny to decision+reason") {
    json claude;
    claude["hookSpecificOutput"]["hookEventName"] = "PreToolUse";
    claude["hookSpecificOutput"]["permissionDecision"] = "deny";
    claude["hookSpecificOutput"]["permissionDecisionReason"] = "use icmg context";
    json g = icmg::cli::geminihooks::toGeminiPre(claude);
    ASSERT_TRUE(g["decision"] == "deny");
    ASSERT_TRUE(g["reason"] == "use icmg context");
    ASSERT_TRUE(!g.contains("hookSpecificOutput"));
}

TEST("gemini-hooks: toGeminiPre maps updatedInput to tool_input merge") {
    json claude;
    claude["hookSpecificOutput"]["hookEventName"] = "PreToolUse";
    claude["hookSpecificOutput"]["permissionDecision"] = "allow";
    claude["hookSpecificOutput"]["updatedInput"]["file_path"] = "/x/y.cpp";
    claude["hookSpecificOutput"]["updatedInput"]["limit"] = 30;
    claude["hookSpecificOutput"]["additionalContext"] = "STRICT: capped";
    json g = icmg::cli::geminihooks::toGeminiPre(claude);
    // merge semantics: only overridden fields, file_path renamed for gemini
    ASSERT_TRUE(g["hookSpecificOutput"]["tool_input"]["limit"] == 30);
    ASSERT_TRUE(g["hookSpecificOutput"]["tool_input"]["absolute_path"] == "/x/y.cpp");
    ASSERT_TRUE(g["systemMessage"] == "STRICT: capped");
    ASSERT_TRUE(!g.contains("decision"));
}

TEST("gemini-hooks: toGeminiPre advisory-only output stays minimal") {
    json claude;
    claude["hookSpecificOutput"]["hookEventName"] = "PreToolUse";
    claude["hookSpecificOutput"]["permissionDecision"] = "allow";
    claude["hookSpecificOutput"]["additionalContext"] = "[dedup] already read";
    json g = icmg::cli::geminihooks::toGeminiPre(claude);
    ASSERT_TRUE(g["systemMessage"] == "[dedup] already read");
    ASSERT_TRUE(!g.contains("hookSpecificOutput"));
    ASSERT_TRUE(!g.contains("decision"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
