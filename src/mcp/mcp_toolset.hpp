#pragma once
// v2.0.0 externals (Dynamic Toolsets): pure selector for which MCP tools to expose
// in tools/list, so a client can shrink the schema payload (41 tools) to a relevant
// subset. Env-driven by the server:
//   ICMG_MCP_TOOLS=a,b,c   explicit allowlist (wins over profile)
//   ICMG_MCP_PROFILE=core  curated essentials; unset / unknown = all (back-compat)
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>
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

// ---- 2026-09-23 IDE-G: per-tool CALL policy ---------------------------------
// The MCP security literature's recurring finding (authorization architectures,
// tenant isolation, capability leases): exposure filtering is NOT access
// control. Our profile/allowlist only hides SCHEMAS from tools/list -- every
// tool stays callable by name. This gate holds at tools/call time:
//   ICMG_MCP_DENY=a,b,c   deny-list enforced on calls (schemas also hidden)
//   ICMG_MCP_READONLY=1   deny the mutating subset (writes/syncs/applies)
// Deterministic, env-driven, default-off -- existing setups are untouched.
// #292: `icmg mcp serve --read-only` / `icmg --mcp-server --read-only` set
// the same env, so the flag and the env var are one switch.
inline constexpr const char* kMcpReadOnlyEnv = "ICMG_MCP_READONLY";

inline bool mcpReadOnlyFromEnv() {
    const char* ro = std::getenv(kMcpReadOnlyEnv);
    return ro && std::string(ro) == "1";
}

inline void enableMcpReadOnly() {
#ifdef _WIN32
    _putenv_s(kMcpReadOnlyEnv, "1");
#else
    setenv(kMcpReadOnlyEnv, "1", 1);
#endif
}

// Tools that MUTATE state (memory writes, syncs, file-writing appliers).
// Read/query/search/render tools stay callable in readonly mode.
inline bool isMutatingTool(const std::string& name) {
    static const std::set<std::string> k = {
        "icmg_store", "icmg_forget", "icmg_correction", "icmg_distill",
        "icmg_sync", "icmg_ingest", "icmg_feedback_record", "icmg_fail",
        "icmg_receipt", "icmg_rule_apply", "icmg_port_apply",
        "icmg_style_clone_apply", "icmg_project_switch", "icmg_write_mode"};
    return k.count(name) > 0;
}

// Should `name` be refused at tools/call? `deny_csv` = ICMG_MCP_DENY (comma
// separated, spaces tolerated); `readonly` = ICMG_MCP_READONLY=1.
inline bool csvHas(const std::string& csv, const std::string& name) {
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const auto b = item.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        const auto e = item.find_last_not_of(" \t");
        if (item.substr(b, e - b + 1) == name) return true;
    }
    return false;
}

inline bool isToolCallDenied(const std::string& name, const std::string& deny_csv,
                             bool readonly) {
    if (readonly && isMutatingTool(name)) return true;
    return !deny_csv.empty() && csvHas(deny_csv, name);
}

// ---- #294: call-time allowlist + no-egress switch ---------------------------
//   ICMG_MCP_ALLOW=a,b,c      only these tools are callable (and listed)
//   ICMG_MCP_NO_NETWORK=1     refuse tools that reach the network / a model
// Flag twins: `icmg mcp serve --tools a,b,c --no-network` (main.cpp).
inline constexpr const char* kMcpAllowEnv     = "ICMG_MCP_ALLOW";
inline constexpr const char* kMcpNoNetworkEnv = "ICMG_MCP_NO_NETWORK";
inline constexpr const char* kMcpDenyEnv      = "ICMG_MCP_DENY";

// Tools with an egress path: outbound HTTP, a model call, a remote sync peer.
inline bool isEgressTool(const std::string& name) {
    static const std::set<std::string> k = {"icmg_fetch", "icmg_batch", "icmg_sync"};
    return k.count(name) > 0;
}

struct McpCallPolicy {
    std::string deny_csv;
    std::string allow_csv;    // empty = no allowlist
    bool readonly   = false;
    bool no_network = false;
};

inline McpCallPolicy mcpCallPolicyFromEnv() {
    auto env = [](const char* k) { const char* v = std::getenv(k); return std::string(v ? v : ""); };
    McpCallPolicy p;
    p.deny_csv   = env(kMcpDenyEnv);
    p.allow_csv  = env(kMcpAllowEnv);
    p.readonly   = mcpReadOnlyFromEnv();
    p.no_network = env(kMcpNoNetworkEnv) == "1";
    return p;
}

// "" = allowed; else the rule that refuses it (shown in the call error).
inline std::string toolDenyReason(const std::string& name, const McpCallPolicy& p) {
    if (p.readonly && isMutatingTool(name))   return "read-only mode";
    if (p.no_network && isEgressTool(name))   return "no-network mode";
    if (!p.deny_csv.empty() && csvHas(p.deny_csv, name)) return "ICMG_MCP_DENY";
    if (!p.allow_csv.empty() && !csvHas(p.allow_csv, name)) return "not in ICMG_MCP_ALLOW";
    return "";
}

inline void setMcpEnv(const char* key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}

// Parse the serve flags (--read-only, --no-network, --tools a,b / --tools=a,b)
// into env, so flags and env vars are one switch. Returns false + `err` on a
// --tools with no value.
inline bool applyMcpServeFlags(const std::vector<std::string>& args, std::string* err) {
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--read-only") enableMcpReadOnly();
        else if (a == "--no-network") setMcpEnv(kMcpNoNetworkEnv, "1");
        else if (a == "--tools" || a.rfind("--tools=", 0) == 0) {
            std::string v = a == "--tools" ? (i + 1 < args.size() ? args[++i] : "")
                                           : a.substr(8);
            if (v.empty() || v[0] == '-') {
                if (err) *err = "--tools needs a comma-separated tool list";
                return false;
            }
            setMcpEnv(kMcpAllowEnv, v);
        }
    }
    return true;
}

}  // namespace icmg::mcp
