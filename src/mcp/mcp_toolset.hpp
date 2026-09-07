#pragma once
// v2.0.0 externals (Dynamic Toolsets): pure selector for which MCP tools to expose
// in tools/list, so a client can shrink the schema payload (41 tools) to a relevant
// subset. Env-driven by the server:
//   ICMG_MCP_TOOLS=a,b,c   explicit allowlist (wins over profile)
//   ICMG_MCP_PROFILE=core  curated essentials; unset / unknown = all (back-compat)
#include <algorithm>
#include <cctype>
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
        // 2026-09-07 B: discovery meta-tool MUST ride along with any lean
        // profile, or hidden tools become unreachable-by-ignorance.
        "icmg_tool_search",
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

// ---------------------------------------------------------------------------
// 2026-09-07 token-killer B (SCOUT pattern, arXiv 2608.23992): when a lean
// profile hides tools, the host still needs to FIND them. rankToolMatches is
// the pure scorer behind the icmg_tool_search meta-tool: word-overlap between
// query and name+description, exact-name and name-substring boosted. The
// hidden tool stays callable (tools/call never filters); only its schema is
// deferred until searched for -- schemas-on-demand instead of 42 up front.
// ---------------------------------------------------------------------------

struct ToolSearchEntry {
    std::string name;
    std::string description;
};

struct ToolSearchHit {
    std::string name;
    double      score = 0.0;
};

inline std::vector<ToolSearchHit> rankToolMatches(
        const std::vector<ToolSearchEntry>& entries,
        const std::string& query,
        int max_out = 5) {
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    };
    auto words = [&](const std::string& s) {
        std::set<std::string> out;
        std::string cur;
        for (char c : s) {
            if (std::isalnum((unsigned char)c)) cur += c;
            else { if (cur.size() >= 3) out.insert(cur); cur.clear(); }
        }
        if (cur.size() >= 3) out.insert(cur);
        return out;
    };
    const std::string q = lower(query);
    const auto qwords = words(q);
    std::vector<ToolSearchHit> hits;
    for (const auto& e : entries) {
        const std::string nm = lower(e.name);
        double score = 0.0;
        if (nm == q) score += 10.0;                       // exact tool name
        else if (nm.find(q) != std::string::npos && !q.empty())
            score += 5.0;                                  // name substring
        const auto twords = words(nm + " " + lower(e.description));
        for (const auto& w : qwords)
            if (twords.count(w)) score += 1.0;             // word overlap
        if (score > 0.0) hits.push_back({e.name, score});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const ToolSearchHit& a, const ToolSearchHit& b) {
                         return a.score > b.score;
                     });
    if ((int)hits.size() > max_out) hits.resize(max_out);
    return hits;
}

}  // namespace icmg::mcp
