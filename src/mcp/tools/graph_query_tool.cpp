#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../graph/graph_store.hpp"
#include <algorithm>

namespace icmg::mcp {

// v2.20 research #7: expose the precomputed code graph as a deterministic
// MULTI-HOP structural-search tool. Frontier code models do fuzzy/local lookup
// well with agentic grep, but cheap deterministic multi-hop traversal
// (who-depends-on-me, blast radius, shortest dependency path) is exactly what a
// precomputed graph answers in one query and an LLM cannot reconstruct cheaply.
//
// One tool, three ops (anti-dup: not three parallel tools):
//   blast_radius  -> transitive set of files that depend on `path` (reverse
//                    closure); `depth` bounds hops.
//   who_calls     -> direct (1-hop) dependents of `path`.
//   path_between  -> shortest dependency path from `path` to `to`.
class GraphQueryTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_graph_query"; }
    std::string description() const override {
        return "Deterministic multi-hop code-graph query: blast_radius | "
               "who_calls | path_between. Cheap structural traversal a code "
               "model cannot reconstruct from flat search.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"op",    "string",  "blast_radius | who_calls | path_between", true},
            {"path",  "string",  "Source file path",                        true},
            {"to",    "string",  "Target path (path_between only)",         false},
            {"depth", "integer", "Max hops (blast_radius, default 3)",      false},
            {"limit", "integer", "Max results (default 25)",                false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "op", 32);
        requireStr(args, "path", 2000);
    }

    json callImpl(const json& args, core::Db& db) override {
        std::string op   = getStr(args, "op");
        std::string path = getStr(args, "path");
        int depth = std::max(1, std::min(10, getInt(args, "depth", 3)));
        int limit = std::max(1, std::min(200, getInt(args, "limit", 25)));

        graph::GraphStore store(db);

        if (op == "blast_radius" || op == "who_calls") {
            int d = (op == "who_calls") ? 1 : depth;
            auto nodes = store.impact(path, d);   // reverse closure: dependents
            json arr = json::array();
            for (auto& n : nodes) {
                if ((int)arr.size() >= limit) break;
                arr.push_back({{"id", n.id}, {"path", n.path}, {"lang", n.lang}});
            }
            return {{"op", op}, {"path", path}, {"depth", d},
                    {"dependents", arr}, {"count", (int)arr.size()}};
        }

        if (op == "path_between") {
            if (!args.contains("to") || !args["to"].is_string() ||
                args["to"].get<std::string>().empty()) {
                return {{"error", "path_between requires 'to'"}};
            }
            std::string to = getStr(args, "to");
            auto p = store.shortestPath(path, to);
            json arr = json::array();
            for (auto& step : p) arr.push_back(step);
            return {{"op", op}, {"from", path}, {"to", to},
                    {"path", arr}, {"hops", arr.empty() ? -1 : (int)arr.size() - 1},
                    {"reachable", !arr.empty()}};
        }

        return {{"error", "unknown op '" + op +
                          "' (blast_radius|who_calls|path_between)"}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_graph_query", GraphQueryTool);

} // namespace icmg::mcp
