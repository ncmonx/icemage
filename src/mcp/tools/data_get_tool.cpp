#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../data/data_store.hpp"

namespace icmg::mcp {

class DataGetTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_data_get"; }
    std::string description() const override {
        return "Get structured data (model/view/behavior/schema) by name.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"name", "string", "Structured data name",             true},
            {"type", "string", "Filter by type (model/view/etc.)", false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "name", 200);
    }

    json callImpl(const json& args, core::Db& db) override {
        std::string name = getStr(args, "name");
        std::string type = getStr(args, "type");

        data::DataStore store(db);
        auto item = store.get(name);

        if (!item) {
            // Try search
            auto results = store.search(name, 5);
            if (results.empty()) return {{"found", false}, {"name", name}};
            // Return first match
            item = results[0];
        }

        return {
            {"found",      true},
            {"id",         item->id},
            {"name",       item->name},
            {"data_type",  item->data_type},
            {"content",    item->content},
            {"version",    item->version},
            {"scope_path", item->scope_path},
            {"tags",       item->tags}
        };
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_data_get", DataGetTool);

} // namespace icmg::mcp
