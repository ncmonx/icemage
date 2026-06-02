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

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
