#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../imem/memory_store.hpp"
#include "../../imem/memory_node.hpp"

namespace icmg::mcp {

class StoreTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_store"; }
    std::string description() const override {
        return "Store a memory node. Use for decisions, resolved errors, "
               "preferences, and significant task completions.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"topic",      "string",  "Topic label (namespace-style, e.g. errors-resolved)", true},
            {"content",    "string",  "Memory content",                                     true},
            {"importance", "string",  "low|med|high|crit (default: med)",                   false},
            {"keywords",   "string",  "Comma-separated keywords for retrieval",              false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "topic",   200);
        requireStr(args, "content", 1024 * 1024);
    }

    bool isMutating() const override { return true; }

    json callImpl(const json& args, core::Db& db) override {
        imem::MemoryNode node;
        node.topic      = getStr(args, "topic");
        node.content    = getStr(args, "content");
        node.keywords   = getStr(args, "keywords");
        node.importance = imem::importanceFromName(getStr(args, "importance", "med"));

        imem::MemoryStore store(db);
        int64_t id = store.store(node, false);
        return {{"id", id}, {"topic", node.topic}, {"status", "stored"}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_store", StoreTool);

} // namespace icmg::mcp
