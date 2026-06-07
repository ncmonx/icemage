// 2026-06-07: pure helpers for `icmg strict audit` read-only deny mode.
// Audit sessions are read-only -> native full-file Read can be hard-denied
// (forces `icmg context`, ~80% token cut) without breaking Edit, and heavy
// browser-automation MCP payloads (puppeteer/playwright) that bypass the
// bash filter can be denied pre-charge.
#include "../test_main.hpp"
#include "../../src/cli/strict_audit.hpp"
#include <filesystem>
#include <fstream>

using namespace icmg::cli::strict_audit;
namespace fs = std::filesystem;

TEST("strict_audit: readShouldDeny denies full read, allows targeted") {
    // audit ON + full read (no offset/limit) -> deny
    ASSERT_EQ(readShouldDeny(true, false), true);
    // audit ON + targeted slice -> already frugal, allow
    ASSERT_EQ(readShouldDeny(true, true), false);
    // audit OFF -> never deny
    ASSERT_EQ(readShouldDeny(false, false), false);
    ASSERT_EQ(readShouldDeny(false, true), false);
}

TEST("strict_audit: isHeavyBrowserMcp matches browser-automation tools") {
    ASSERT_EQ(isHeavyBrowserMcp("mcp__puppeteer__navigate"), true);
    ASSERT_EQ(isHeavyBrowserMcp("mcp__playwright__screenshot"), true);
    ASSERT_EQ(isHeavyBrowserMcp("BROWSER_get_dom"), true);          // case-insensitive
    // non-browser MCP tools must NOT be denied
    ASSERT_EQ(isHeavyBrowserMcp("mcp__supabase__query"), false);
    ASSERT_EQ(isHeavyBrowserMcp("mcp__context7__resolve"), false);
    ASSERT_EQ(isHeavyBrowserMcp(""), false);
}

TEST("strict_audit: mcpShouldDeny gated by audit flag") {
    ASSERT_EQ(mcpShouldDeny(true,  "mcp__puppeteer__navigate"), true);
    ASSERT_EQ(mcpShouldDeny(false, "mcp__puppeteer__navigate"), false); // off -> allow
    ASSERT_EQ(mcpShouldDeny(true,  "mcp__supabase__query"), false);     // not heavy
}

TEST("strict_audit: flagActive honors env then flag file") {
    fs::path tmp = fs::temp_directory_path() /
                   ("icmg-audit-test-" + std::to_string((long long)9281));
    fs::remove(tmp);
    // neither env nor file -> inactive
    ASSERT_EQ(flagActive(tmp, nullptr), false);
    ASSERT_EQ(flagActive(tmp, "0"), false);
    // env=1 -> active even without file
    ASSERT_EQ(flagActive(tmp, "1"), true);
    // file present -> active even without env
    { std::ofstream f(tmp); f << "1\n"; }
    ASSERT_EQ(flagActive(tmp, nullptr), true);
    fs::remove(tmp);
}
