#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../graph/graph_store.hpp"

namespace icmg::mcp {

class GraphRelatedTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_graph_related"; }
    std::string description() const override {
        return "Find files related to a given file via graph edges.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"path",  "string",  "File path",                 true},
            {"limit", "integer", "Max results (default 10)",  false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "path", 2000);
    }

    json callImpl(const json& args, core::Db& db) override {
        std::string path = getStr(args, "path");
        int limit        = getInt(args, "limit", 10);
        limit = std::max(1, std::min(50, limit));

        graph::GraphStore store(db);
        auto nodes = store.related(path, limit);

        json arr = json::array();
        for (auto& n : nodes) {
            arr.push_back({
                {"id",      n.id},
                {"path",    n.path},
                {"lang",    n.lang},
                {"context", n.context}
            });
        }
        return {{"related", arr}, {"count", (int)arr.size()}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_graph_related", GraphRelatedTool);

} // namespace icmg::mcp
