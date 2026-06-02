#pragma once
// v2.0.0 externals (Dynamic Toolsets): pure selector for which MCP tools to expose
// in tools/list, so a client can shrink the schema payload (41 tools) to a relevant
// subset. Env-driven by the server:
//   ICMG_MCP_TOOLS=a,b,c   explicit allowlist (wins over profile)
//   ICMG_MCP_PROFILE=core  curated essentials; unset / unknown = all (back-compat)
#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace icmg::mcp {

// Curated "core" toolset — the day-to-day essentials an agent needs most.
inline const std::set<std::string>& coreToolset() {
    static const std::set<std::string> kCore = {
        "icmg_recall", "icmg_store", "icmg_graph_context", "icmg_graph_related",
        "icmg_code_search", "icmg_compress", "icmg_savings", "icmg_fetch",
        "icmg_ingest", "icmg_sync",
    };
    return kCore;
}

// Filter `all` tool names. `csv` (explicit allowlist) wins; else `profile` ("core"
// = curated subset, anything else = all). Output preserves `all` ordering.
inline std::vector<std::string> selectExposedTools(const std::vector<std::string>& all,
                                                   const std::string& profile,
                                                   const std::string& csv) {
    if (!csv.empty()) {
        std::set<std::string> want;
        std::string cur;
        for (char ch : csv) {
            if (ch == ',') { if (!cur.empty()) want.insert(cur); cur.clear(); }
            else if (ch != ' ') cur.push_back(ch);
        }
        if (!cur.empty()) want.insert(cur);
        std::vector<std::string> out;
        for (const auto& n : all) if (want.count(n)) out.push_back(n);
        return out;
    }
    if (profile == "core") {
        const auto& core = coreToolset();
        std::vector<std::string> out;
        for (const auto& n : all) if (core.count(n)) out.push_back(n);
        return out;
    }
    return all;  // "all" / unset / unknown profile
}

}  // namespace icmg::mcp
