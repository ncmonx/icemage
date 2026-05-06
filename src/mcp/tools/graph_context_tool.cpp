#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../graph/graph_store.hpp"

namespace icmg::mcp {

class GraphContextTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_graph_context"; }
    std::string description() const override {
        return "Get context, symbols, and metadata for a file in the knowledge graph.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"path", "string", "File path (relative or absolute)", true},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "path", 2000);
        // A3: basic path sanity (no null bytes)
        auto p = getStr(args, "path");
        if (p.find('\0') != std::string::npos)
            throw McpError(-32602, "path contains null byte");
    }

    json callImpl(const json& args, core::Db& db) override {
        std::string path = getStr(args, "path");

        graph::GraphStore store(db);
        auto node = store.getNode(path);

        if (!node) {
            // Try case-insensitive substring search
            auto results = store.search(path, 1);
            if (!results.empty()) node = results[0];
        }

        if (!node) {
            return {{"found", false}, {"path", path}};
        }

        // edges
        auto edges_from = store.edgesFrom(node->id);
        json deps = json::array();
        for (auto& e : edges_from) {
            deps.push_back({{"dst", e.dst}, {"type", e.edge_type}, {"weight", e.weight}});
        }

        return {
            {"found",        true},
            {"id",           node->id},
            {"path",         node->path},
            {"lang",         node->lang},
            {"context",      node->context},
            {"symbols",      node->symbols},
            {"size_bytes",   node->size_bytes},
            {"access_count", node->access_count},
            {"dependencies", deps}
        };
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_graph_context", GraphContextTool);

} // namespace icmg::mcp
