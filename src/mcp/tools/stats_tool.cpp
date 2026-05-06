#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"

namespace icmg::mcp {

class StatsTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_stats"; }
    std::string description() const override {
        return "Get statistics for the current project DB (row counts, top topics, top commands).";
    }
    std::vector<McpToolParam> params() const override { return {}; }

protected:
    json callImpl(const json& /*args*/, core::Db& db) override {
        json stats = json::object();

        // Row counts for each core table
        auto count = [&](const char* table) -> int {
            int n = 0;
            db.query(std::string("SELECT COUNT(*) FROM ") + table,
                     {}, [&](const core::Row& r) {
                         if (!r.empty()) try { n = std::stoi(r[0]); } catch (...) {}
                     });
            return n;
        };

        stats["memory_nodes"]      = count("memory_nodes");
        stats["graph_nodes"]       = count("graph_nodes");
        stats["graph_edges"]       = count("graph_edges");
        stats["rules"]             = count("rules");
        stats["structured_data"]   = count("structured_data");
        stats["abbreviations"]     = count("abbreviations");
        stats["stored_procedures"] = count("stored_procedures");
        stats["commands"]          = count("commands");

        // Top 5 memory topics by frequency
        json top_topics = json::array();
        db.query("SELECT topic, SUM(frequency) AS freq FROM memory_nodes"
                 " WHERE deleted_at IS NULL GROUP BY topic"
                 " ORDER BY freq DESC LIMIT 5",
                 {}, [&](const core::Row& r) {
                     if (r.size() >= 2) {
                         int freq = 0;
                         try { freq = std::stoi(r[1]); } catch (...) {}
                         top_topics.push_back({{"topic", r[0]}, {"frequency", freq}});
                     }
                 });
        stats["top_topics"] = top_topics;

        // Top 5 RTK commands by frequency
        json top_cmds = json::array();
        db.query("SELECT command, frequency FROM commands"
                 " ORDER BY frequency DESC LIMIT 5",
                 {}, [&](const core::Row& r) {
                     if (r.size() >= 2) {
                         int freq = 0;
                         try { freq = std::stoi(r[1]); } catch (...) {}
                         top_cmds.push_back({{"command", r[0]}, {"frequency", freq}});
                     }
                 });
        stats["top_commands"] = top_cmds;

        // Graph lang breakdown
        json langs = json::array();
        db.query("SELECT lang, COUNT(*) AS cnt FROM graph_nodes"
                 " WHERE lang IS NOT NULL GROUP BY lang ORDER BY cnt DESC LIMIT 10",
                 {}, [&](const core::Row& r) {
                     if (r.size() >= 2) {
                         int cnt = 0;
                         try { cnt = std::stoi(r[1]); } catch (...) {}
                         langs.push_back({{"lang", r[0]}, {"count", cnt}});
                     }
                 });
        stats["graph_lang_breakdown"] = langs;

        return stats;
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_stats", StatsTool);

} // namespace icmg::mcp
