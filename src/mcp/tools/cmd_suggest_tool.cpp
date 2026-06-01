#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"

namespace icmg::mcp {

class CmdSuggestTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_cmd_suggest"; }
    std::string description() const override {
        return "Suggest frequently used Tkil commands, optionally filtered by prefix.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"prefix", "string",  "Command prefix to filter by",   false},
            {"limit",  "integer", "Max results (default 10)",       false},
        };
    }

protected:
    json callImpl(const json& args, core::Db& db) override {
        std::string prefix = getStr(args, "prefix");
        int limit          = getInt(args, "limit", 10);
        limit = std::max(1, std::min(50, limit));

        std::vector<std::pair<std::string, int>> cmds;

        if (!prefix.empty()) {
            std::string like = prefix + "%";
            db.query("SELECT command, frequency FROM commands WHERE command LIKE ?"
                     " ORDER BY frequency DESC LIMIT ?",
                     {like, std::to_string(limit)},
                     [&](const core::Row& r) {
                         if (r.size() >= 2) {
                             int freq = 0;
                             try { freq = std::stoi(r[1]); } catch (...) {}
                             cmds.push_back({r[0], freq});
                         }
                     });
        } else {
            db.query("SELECT command, frequency FROM commands"
                     " ORDER BY frequency DESC LIMIT ?",
                     {std::to_string(limit)},
                     [&](const core::Row& r) {
                         if (r.size() >= 2) {
                             int freq = 0;
                             try { freq = std::stoi(r[1]); } catch (...) {}
                             cmds.push_back({r[0], freq});
                         }
                     });
        }

        json arr = json::array();
        for (auto& [cmd, freq] : cmds) {
            arr.push_back({{"command", cmd}, {"frequency", freq}});
        }
        return {{"commands", arr}, {"count", (int)arr.size()}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_cmd_suggest", CmdSuggestTool);

} // namespace icmg::mcp
