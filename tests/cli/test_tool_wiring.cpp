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
