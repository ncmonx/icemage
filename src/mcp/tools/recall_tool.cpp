#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../icm/memory_store.hpp"
#include "../../icm/memory_node.hpp"

namespace icmg::mcp {

class RecallTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_recall"; }
    std::string description() const override {
        return "Recall relevant memory nodes by BM25+recency ranking. "
               "Use to retrieve past decisions, errors, preferences, project context.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"query",  "string",  "Search query",                    true},
            {"limit",  "integer", "Max results (default 10)",        false},
            {"topic",  "string",  "Filter by topic prefix",          false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "query", 1000);
        if (args.contains("limit") && !args["limit"].is_number_integer())
            throw McpError(-32602, "limit must be integer");
    }

    json callImpl(const json& args, core::Db& db) override {
        std::string query = getStr(args, "query");
        int limit         = getInt(args, "limit", 10);
        std::string topic = getStr(args, "topic");
        limit = std::max(1, std::min(50, limit));

        icm::MemoryStore store(db);
        std::vector<icm::MemoryNode> nodes;
        if (!topic.empty()) {
            nodes = store.recallByTopic(topic, limit);
        } else {
            nodes = store.recall(query, limit);
        }

        json arr = json::array();
        for (auto& n : nodes) {
            arr.push_back({
                {"id",         n.id},
                {"topic",      n.topic},
                {"content",    n.content},
                {"keywords",   n.keywords},
                {"importance", n.importance},
                {"frequency",  n.frequency},
                {"score",      n.score}
            });
        }
        return {{"nodes", arr}, {"count", (int)arr.size()}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_recall", RecallTool);

} // namespace icmg::mcp
