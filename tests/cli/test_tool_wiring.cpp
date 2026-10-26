// tests/cli/test_tool_wiring.cpp
//
// TDD tests for multi-tool wiring (gap G6: `icmg init --all-tools`).
// Pure helpers in src/cli/tool_wiring.hpp — no filesystem side effects except
// the detection probe, which we exercise against a temp dir.

#include "../test_main.hpp"
#include "../../src/cli/tool_wiring.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;
using namespace icmg::cli::toolwiring;

static bool hasTool(const std::vector<ToolTarget>& v, const std::string& n) {
    return std::any_of(v.begin(), v.end(),
                       [&](const ToolTarget& t){ return t.name == n; });
}

TEST("tool-wiring: known tools include the documented host CLIs") {
    auto tools = knownTools();
    ASSERT_TRUE(tools.size() >= 8);
    for (const char* n : {"cursor","windsurf","zed","codex",
                          "copilot","opencode","gemini","amp"}) {
        ASSERT_TRUE(hasTool(tools, n));
    }
}

TEST("tool-wiring: every tool has a config path and non-empty routing content") {
    for (const auto& t : knownTools()) {
        ASSERT_TRUE(!t.configRelPath.empty());
        std::string c = routingContent(t.name);
        ASSERT_TRUE(!c.empty());
        // routing text should mention icmg so the agent is pointed at it
        ASSERT_CONTAINS(c, "icmg");
    }
}

TEST("tool-wiring: unknown tool yields empty routing content") {
    ASSERT_TRUE(routingContent("no-such-tool").empty());
}

TEST("tool-wiring: cursor config path is project-level .cursor rule") {
    ToolTarget ct;
    bool found = false;
    for (const auto& t : knownTools()) if (t.name == "cursor") { ct = t; found = true; }
    ASSERT_TRUE(found);
    ASSERT_TRUE(ct.projectLevel);
    ASSERT_CONTAINS(ct.configRelPath, ".cursor");
}

TEST("tool-wiring: detect present when project config dir/file exists") {
    fs::path d = fs::temp_directory_path() / ("icmg_toolwire_" +
                 std::to_string((uintptr_t)&d));
    std::error_code ec; fs::remove_all(d, ec);
    fs::create_directories(d / ".cursor" / "rules", ec);

    ToolTarget cursor;
    for (const auto& t : knownTools()) if (t.name == "cursor") cursor = t;

    // present: .cursor exists under project root
    ASSERT_TRUE(isToolPresent(cursor, d, d /*home*/));

    // absent: a windsurf marker not created
    ToolTarget windsurf;
    for (const auto& t : knownTools()) if (t.name == "windsurf") windsurf = t;
    ASSERT_TRUE(!isToolPresent(windsurf, d, d));

    fs::remove_all(d, ec);
}

// ---------------------------------------------------------------------------
// v2.27 host-compat audit (2026-10-26): wiring must match what each host
// actually READS in 2026. New hosts: cline, antigravity, roo, kilo, junie.
// Fixed targets: gemini -> GEMINI.md, windsurf -> .windsurf/rules/,
// AGENTS.md readers (zed/codex/amp/opencode) -> managed AGENTS.md block.
// ---------------------------------------------------------------------------

static ToolTarget getTool(const std::string& n) {
    for (const auto& t : knownTools()) if (t.name == n) return t;
    return {};
}

TEST("tool-wiring v2: new hosts cline/antigravity/roo/kilo/junie are known") {
    auto tools = knownTools();
    ASSERT_TRUE(tools.size() >= 13);
    for (const char* n : {"cline","antigravity","roo","kilo","junie"})
        ASSERT_TRUE(hasTool(tools, n));
}

TEST("tool-wiring v2: gemini writes GEMINI.md at project root (managed block)") {
    ToolTarget g = getTool("gemini");
    ASSERT_TRUE(g.configRelPath == "GEMINI.md");
    ASSERT_TRUE(g.projectLevel);
    ASSERT_TRUE(g.managedBlock);
}

TEST("tool-wiring v2: windsurf uses .windsurf/rules/ not deprecated .windsurfrules") {
    ToolTarget w = getTool("windsurf");
    ASSERT_CONTAINS(w.configRelPath, ".windsurf/rules/");
    // legacy marker still detects the tool
    fs::path d = fs::temp_directory_path() / "icmg_toolwire_ws_legacy";
    std::error_code ec; fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    { std::ofstream f((d / ".windsurfrules").string()); f << "x"; }
    ASSERT_TRUE(isToolPresent(w, d, d));
    fs::remove_all(d, ec);
}

TEST("tool-wiring v2: AGENTS.md readers route via managed AGENTS.md") {
    for (const char* n : {"zed","codex","amp","opencode"}) {
        ToolTarget t = getTool(n);
        ASSERT_TRUE(t.configRelPath == "AGENTS.md");
        ASSERT_TRUE(t.managedBlock);
    }
}

TEST("tool-wiring v2: cline writes .clinerules/ directory rule") {
    ToolTarget c = getTool("cline");
    ASSERT_CONTAINS(c.configRelPath, ".clinerules/");
}

TEST("tool-wiring v2: antigravity rule has YAML frontmatter with trigger") {
    ToolTarget a = getTool("antigravity");
    ASSERT_CONTAINS(a.configRelPath, ".agents/rules/");
    std::string c = routingContent("antigravity");
    ASSERT_TRUE(c.rfind("---", 0) == 0);      // starts with frontmatter
    ASSERT_CONTAINS(c, "trigger:");
    ASSERT_CONTAINS(c, "description:");
    ASSERT_CONTAINS(c, "icmg");
}

static size_t countOccur(const std::string& hay, const std::string& needle) {
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) { ++n; pos += needle.size(); }
    return n;
}

TEST("tool-wiring v2: managed write preserves user content and is idempotent") {
    fs::path d = fs::temp_directory_path() / "icmg_toolwire_managed";
    std::error_code ec; fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    { std::ofstream f((d / "GEMINI.md").string()); f << "# My project notes\nkeep me\n"; }

    ToolTarget g = getTool("gemini");
    fs::path w1 = writeRouting(g, d, d);
    ASSERT_TRUE(!w1.empty());
    std::ifstream in1(w1); std::string b1((std::istreambuf_iterator<char>(in1)), {}); in1.close();
    ASSERT_CONTAINS(b1, "keep me");
    ASSERT_CONTAINS(b1, "icmg");

    // re-run: block replaced, not duplicated
    fs::path w2 = writeRouting(g, d, d);
    ASSERT_TRUE(!w2.empty());
    std::ifstream in2(w2); std::string b2((std::istreambuf_iterator<char>(in2)), {}); in2.close();
    ASSERT_TRUE(countOccur(b2, "<!-- icmg-wiring:start -->") == 1);
    ASSERT_CONTAINS(b2, "keep me");
    fs::remove_all(d, ec);
}

TEST("tool-wiring v2: managed write is a no-op when main icmg block already present") {
    fs::path d = fs::temp_directory_path() / "icmg_toolwire_noop";
    std::error_code ec; fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    std::string orig = "# AGENTS\n<!-- icmg:start -->\nfull routing here\n<!-- icmg:end -->\n";
    { std::ofstream f((d / "AGENTS.md").string()); f << orig; }

    ToolTarget z = getTool("zed");
    fs::path w = writeRouting(z, d, d);
    ASSERT_TRUE(!w.empty());
    std::ifstream in(w); std::string body((std::istreambuf_iterator<char>(in)), {}); in.close();
    ASSERT_TRUE(body == orig);   // untouched: init's full block already routes
    fs::remove_all(d, ec);
}

TEST("tool-wiring: writeRouting creates the config file with content") {
    fs::path d = fs::temp_directory_path() / ("icmg_toolwire_w_" +
                 std::to_string((uintptr_t)&d));
    std::error_code ec; fs::remove_all(d, ec);
    fs::create_directories(d, ec);

    ToolTarget cursor;
    for (const auto& t : knownTools()) if (t.name == "cursor") cursor = t;

    fs::path written = writeRouting(cursor, d, d);
    ASSERT_TRUE(!written.empty());
    ASSERT_TRUE(fs::exists(written, ec));

    std::ifstream in(written);
    std::string body((std::istreambuf_iterator<char>(in)), {});
    ASSERT_CONTAINS(body, "icmg");

    fs::remove_all(d, ec);
}
