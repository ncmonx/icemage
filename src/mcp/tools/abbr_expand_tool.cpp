#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../abbreviation/abbr_store.hpp"

namespace icmg::mcp {

class AbbrExpandTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_abbr_expand"; }
    std::string description() const override {
        return "Expand abbreviations in text using the project abbreviation dictionary.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"text", "string", "Text to expand abbreviations in", true},
            {"cwd",  "string", "Working dir for scope priority",  false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "text", 50000);
    }

    json callImpl(const json& args, core::Db& db) override {
        std::string text = getStr(args, "text");
        std::string cwd  = getStr(args, "cwd");

        abbreviation::AbbrStore store(db);
        std::string expanded = store.expand(text, cwd);

        return {
            {"original", text},
            {"expanded", expanded},
            {"changed",  expanded != text}
        };
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_abbr_expand", AbbrExpandTool);

} // namespace icmg::mcp
