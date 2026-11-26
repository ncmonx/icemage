// v2.0.0 externals (Dynamic Toolsets): expose only a relevant SUBSET of MCP tools
// so the tools/list payload (41 schemas) shrinks. Pure selector — env-driven:
//   ICMG_MCP_TOOLS=csv   explicit allowlist (wins)
//   ICMG_MCP_PROFILE=core curated essentials; anything else / unset = all (back-compat)

#include "../test_main.hpp"
#include "../../src/mcp/mcp_toolset.hpp"

#include <string>
#include <vector>
#include <algorithm>

using namespace icmg::mcp;

namespace {
std::vector<std::string> ALL{
    "icmg_recall","icmg_store","icmg_graph_context","icmg_code_search",
    "icmg_compress","icmg_savings","icmg_fetch","icmg_zzz_other","icmg_misc_tool"
};
bool has(const std::vector<std::string>& v, const std::string& s){
    return std::find(v.begin(),v.end(),s)!=v.end();
}
}

TEST("toolset: default (all) returns everything") {
    auto r = selectExposedTools(ALL, "", "");
    ASSERT_EQ((int)r.size(), (int)ALL.size());
}

TEST("toolset: profile=core returns curated subset only") {
    auto r = selectExposedTools(ALL, "core", "");
    ASSERT_TRUE(r.size() < ALL.size());
    ASSERT_TRUE(has(r, "icmg_recall"));
    ASSERT_TRUE(has(r, "icmg_code_search"));
    ASSERT_FALSE(has(r, "icmg_zzz_other"));
}

TEST("toolset: explicit csv allowlist wins, preserves all-order") {
    auto r = selectExposedTools(ALL, "core", "icmg_fetch,icmg_store");
    ASSERT_EQ((int)r.size(), 2);
    ASSERT_TRUE(has(r, "icmg_fetch"));
    ASSERT_TRUE(has(r, "icmg_store"));
    ASSERT_FALSE(has(r, "icmg_recall"));
}

TEST("toolset: csv ignores unknown names") {
    auto r = selectExposedTools(ALL, "", "icmg_store,icmg_nope");
    ASSERT_EQ((int)r.size(), 1);
    ASSERT_TRUE(has(r, "icmg_store"));
}

TEST("toolset: unknown profile = all (back-compat)") {
    auto r = selectExposedTools(ALL, "bogus", "");
    ASSERT_EQ((int)r.size(), (int)ALL.size());
}

// ---- 2026-09-07 token-killer B: tool-search ranking (SCOUT pattern) --------

static std::vector<icmg::mcp::ToolSearchEntry> ENTRIES = {
    {"icmg_recall",      "Search project memory for past decisions and fixes"},
    {"icmg_code_search", "Search the code knowledge graph for files/symbols by query"},
    {"icmg_fetch",       "Fetch a URL with cache and token reduction"},
    {"icmg_store",       "Store a memory note under a topic"},
};

TEST("tool_search: exact name wins") {
    auto r = icmg::mcp::rankToolMatches(ENTRIES, "icmg_fetch");
    ASSERT_TRUE(!r.empty());
    ASSERT_EQ(r[0].name, std::string("icmg_fetch"));
}

TEST("tool_search: description words match") {
    auto r = icmg::mcp::rankToolMatches(ENTRIES, "past decisions memory");
    ASSERT_TRUE(!r.empty());
    ASSERT_EQ(r[0].name, std::string("icmg_recall"));
}

TEST("tool_search: name substring beats plain word overlap") {
    auto r = icmg::mcp::rankToolMatches(ENTRIES, "code_search");
    ASSERT_TRUE(!r.empty());
    ASSERT_EQ(r[0].name, std::string("icmg_code_search"));
}

TEST("tool_search: no match = empty, capped output") {
    ASSERT_EQ((int)icmg::mcp::rankToolMatches(ENTRIES, "zzzqqq").size(), 0);
    auto r = icmg::mcp::rankToolMatches(ENTRIES, "search", 2);
    ASSERT_EQ((int)r.size(), 2);
}

// ---- 2026-09-23 IDE-G: per-tool call policy (MCP security theme) -----------
// Exposure filtering (profile/allowlist) only hides SCHEMAS -- every tool
// stayed callable. A deny policy must hold at tools/call time.

TEST("call_policy: deny csv blocks exactly the named tools") {
    ASSERT_TRUE(icmg::mcp::isToolCallDenied("icmg_forget", "icmg_forget,icmg_store", false));
    ASSERT_TRUE(icmg::mcp::isToolCallDenied("icmg_store", "icmg_forget, icmg_store", false));
    ASSERT_TRUE(!icmg::mcp::isToolCallDenied("icmg_recall", "icmg_forget,icmg_store", false));
}

TEST("call_policy: readonly blocks mutating tools only") {
    ASSERT_TRUE(icmg::mcp::isToolCallDenied("icmg_store", "", true));
    ASSERT_TRUE(icmg::mcp::isToolCallDenied("icmg_forget", "", true));
    ASSERT_TRUE(!icmg::mcp::isToolCallDenied("icmg_recall", "", true));
    ASSERT_TRUE(!icmg::mcp::isToolCallDenied("icmg_context", "", true));
    ASSERT_TRUE(!icmg::mcp::isToolCallDenied("icmg_tool_search", "", true));
}

TEST("call_policy: empty policy denies nothing") {
    ASSERT_TRUE(!icmg::mcp::isToolCallDenied("icmg_store", "", false));
    ASSERT_TRUE(!icmg::mcp::isToolCallDenied("icmg_forget", "", false));
}

// #294: locked-down hosts need call-time allowlist + a no-egress switch.
TEST("call_policy: allowlist refuses every tool not on it") {
    icmg::mcp::McpCallPolicy p;
    p.allow_csv = "icmg_recall, icmg_pack,icmg_graph_symbol";
    ASSERT_TRUE(icmg::mcp::toolDenyReason("icmg_recall", p).empty());
    ASSERT_TRUE(icmg::mcp::toolDenyReason("icmg_graph_symbol", p).empty());
    ASSERT_TRUE(!icmg::mcp::toolDenyReason("icmg_fetch", p).empty());
    ASSERT_TRUE(!icmg::mcp::toolDenyReason("icmg_tool_search", p).empty());
}

TEST("call_policy: no_network refuses egress tools only") {
    icmg::mcp::McpCallPolicy p;
    p.no_network = true;
    ASSERT_TRUE(!icmg::mcp::toolDenyReason("icmg_fetch", p).empty());
    ASSERT_TRUE(!icmg::mcp::toolDenyReason("icmg_batch", p).empty());
    ASSERT_TRUE(!icmg::mcp::toolDenyReason("icmg_sync", p).empty());
    ASSERT_TRUE(icmg::mcp::toolDenyReason("icmg_recall", p).empty());
    ASSERT_TRUE(icmg::mcp::toolDenyReason("icmg_store", p).empty());
}

TEST("call_policy: reasons name the rule; readonly and deny still apply") {
    icmg::mcp::McpCallPolicy p;
    p.readonly = true;
    p.deny_csv = "icmg_savings";
    ASSERT_EQ(icmg::mcp::toolDenyReason("icmg_store", p), std::string("read-only mode"));
    ASSERT_EQ(icmg::mcp::toolDenyReason("icmg_savings", p), std::string("ICMG_MCP_DENY"));
    p = {};
    p.no_network = true;
    ASSERT_EQ(icmg::mcp::toolDenyReason("icmg_fetch", p), std::string("no-network mode"));
    p = {};
    p.allow_csv = "icmg_recall";
    ASSERT_EQ(icmg::mcp::toolDenyReason("icmg_store", p), std::string("not in ICMG_MCP_ALLOW"));
}

TEST("call_policy: serve flags set the env twins") {
    std::string err;
    ASSERT_TRUE(icmg::mcp::applyMcpServeFlags({"--no-network", "--tools", "icmg_recall,icmg_pack"}, &err));
    auto p = icmg::mcp::mcpCallPolicyFromEnv();
    ASSERT_TRUE(p.no_network);
    ASSERT_EQ(p.allow_csv, std::string("icmg_recall,icmg_pack"));
    ASSERT_TRUE(icmg::mcp::applyMcpServeFlags({"--tools=icmg_recall"}, &err));
    ASSERT_EQ(icmg::mcp::mcpCallPolicyFromEnv().allow_csv, std::string("icmg_recall"));
    ASSERT_TRUE(!icmg::mcp::applyMcpServeFlags({"--tools"}, &err));
    ASSERT_TRUE(!err.empty());
    icmg::mcp::setMcpEnv(icmg::mcp::kMcpNoNetworkEnv, "");
    icmg::mcp::setMcpEnv(icmg::mcp::kMcpAllowEnv, "");
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
