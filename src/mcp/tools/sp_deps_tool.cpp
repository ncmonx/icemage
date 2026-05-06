#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../sp/sp_store.hpp"
#include <unordered_set>
#include <queue>

namespace icmg::mcp {

class SpDepsTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_sp_deps"; }
    std::string description() const override {
        return "Get the dependency tree for a stored procedure (BFS).";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"sp_name", "string",  "Stored procedure name",        true},
            {"depth",   "integer", "Max BFS depth (default 5)",    false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "sp_name", 200);
    }

    json callImpl(const json& args, core::Db& db) override {
        std::string root_name = getStr(args, "sp_name");
        int max_depth         = getInt(args, "depth", 5);
        max_depth = std::max(1, std::min(10, max_depth));

        sp::SpStore store(db);

        // BFS
        json tree = json::object();
        std::unordered_set<std::string> visited;
        std::queue<std::pair<std::string, int>> q;
        q.push({root_name, 0});

        while (!q.empty()) {
            auto [name, depth] = q.front(); q.pop();
            if (visited.count(name) || depth > max_depth) continue;
            visited.insert(name);

            auto sp = store.get(name);
            json entry;
            entry["name"]  = name;
            entry["found"] = sp.has_value();
            entry["depth"] = depth;
            if (sp) {
                entry["db_type"]  = sp->db_type;
                entry["context"]  = sp->context;
                entry["children"] = json::array();
                for (auto& dep : sp->sp_dependencies) {
                    entry["children"].push_back(dep);
                    if (!visited.count(dep)) q.push({dep, depth + 1});
                }
            }
            tree[name] = entry;
        }

        return {{"root", root_name}, {"tree", tree}, {"nodes_visited", (int)visited.size()}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_sp_deps", SpDepsTool);

} // namespace icmg::mcp
