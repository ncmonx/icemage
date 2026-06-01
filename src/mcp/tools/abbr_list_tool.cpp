#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../abbreviation/abbr_store.hpp"

namespace icmg::mcp {

class AbbrListTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_abbr_list"; }
    std::string description() const override {
        return "List known abbreviations, optionally filtered by domain.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"domain", "string", "Filter by domain (e.g. accounting)", false},
            {"query",  "string", "Search abbreviations by term",        false},
        };
    }

protected:
    json callImpl(const json& args, core::Db& db) override {
        std::string domain = getStr(args, "domain");
        std::string query  = getStr(args, "query");

        abbreviation::AbbrStore store(db);

        std::vector<abbreviation::Abbreviation> abbrs;
        if (!query.empty()) {
            abbrs = store.search(query);
        } else {
            abbrs = store.list(domain);
        }

        json arr = json::array();
        for (auto& a : abbrs) {
            arr.push_back({
                {"id",          a.id},
                {"short_form",  a.short_form},
                {"full_form",   a.full_form},
                {"domain",      a.domain},
                {"scope_path",  a.scope_path},
                {"frequency",   a.frequency}
            });
        }
        return {{"abbreviations", arr}, {"count", (int)arr.size()}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_abbr_list", AbbrListTool);

} // namespace icmg::mcp
