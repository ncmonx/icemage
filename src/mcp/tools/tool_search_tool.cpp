// 2026-09-07 token-killer B (SCOUT pattern, arXiv 2608.23992): tool discovery
// meta-tool. With a lean profile (ICMG_MCP_PROFILE=core) the host sees only
// the core toolset up front -- 42 full schemas would saturate the context
// window before the first query. This tool searches ALL registered tools
// (word overlap over name+description, exact/substring name boosted) and
// returns the full schema per hit, so schemas load on demand. Hidden tools
// remain callable via tools/call (the list filter never gates dispatch).
#include "../base_mcp_tool.hpp"
#include "../mcp_toolset.hpp"
#include "../../core/registry.hpp"

namespace icmg::mcp {

class ToolSearchTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_tool_search"; }
    std::string description() const override {
        return "Find icmg MCP tools by capability query (e.g. 'compress output', "
               "'search memory'). Returns matching tool names, descriptions and full "
               "input schemas. Use when no visible tool fits the need -- hidden tools "
               "are callable once discovered here.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"query", "string",  "What you need the tool to do", true},
            {"limit", "integer", "Max matches (default 5)", false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "query", 500);
    }

    json callImpl(const json& args, core::Db&) override {
        const std::string query = getStr(args, "query");
        int limit = getInt(args, "limit", 5);
        if (limit < 1) limit = 1;
        if (limit > 20) limit = 20;

        auto& reg = core::Registry<BaseMcpTool>::instance();
        std::vector<ToolSearchEntry> entries;
        for (const auto& k : reg.keys()) {
            auto t = reg.create(k);
            entries.push_back({t->name(), t->description()});
        }
        auto hits = rankToolMatches(entries, query, limit);

        json out = json::array();
        for (const auto& h : hits) {
            auto t = reg.create(h.name);
            if (!t) continue;
            out.push_back({
                {"name",        t->name()},
                {"description", t->description()},
                {"inputSchema", t->schema()},
                {"score",       h.score},
            });
        }
        return {{"matches", out}, {"total_registered", entries.size()}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_tool_search", ToolSearchTool);

} // namespace icmg::mcp
