// v2.0.0 externals (Semantic Code Search MCP): expose the knowledge graph's
// symbol/content search to AI agents as an MCP tool, so they locate code by
// query instead of falling back to grep/Read (large token savings).
//
// Keyword-first (deterministic, no model): GraphStore::search ranks file+symbol
// nodes over symbol_name + content. Optional --kind filter. Exact-symbol fallback
// via findSymbol when the query looks like an identifier.
//
// Semantic cosine rerank (graph embeddings via `icmg embed graph`) is a future
// opt-in enhancement — kept out of the default hot path (local-first principle).
#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../graph/graph_store.hpp"

#include <algorithm>

namespace icmg::mcp {

class CodeSearchTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_code_search"; }
    std::string description() const override {
        return "Search the code knowledge graph for files/symbols by query "
               "(returns path, symbol, kind, signature, line). Use instead of grep/Read.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"query", "string", "Search terms (symbol name, identifier, or keywords)", true},
            {"limit", "integer", "Max results (default 10)", false},
            {"kind",  "string",  "Filter by kind: file|class|struct|function|method (optional)", false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "query", 2000);
        if (getStr(args, "query").find('\0') != std::string::npos)
            throw McpError(-32602, "query contains null byte");
    }

    json callImpl(const json& args, core::Db& db) override {
        const std::string query = getStr(args, "query");
        int limit = getInt(args, "limit", 10);
        if (limit < 1) limit = 1;
        if (limit > 100) limit = 100;
        const std::string kind = getStr(args, "kind");

        graph::GraphStore store(db);
        // Over-fetch so the kind filter still yields up to `limit` rows.
        auto nodes = store.search(query, kind.empty() ? limit : limit * 4);

        // Exact-symbol fallback when keyword search is thin and the query reads
        // like a single identifier.
        if (nodes.size() < (size_t)limit &&
            query.find(' ') == std::string::npos) {
            auto syms = store.findSymbol(query);
            for (auto& s : syms) {
                bool dup = std::any_of(nodes.begin(), nodes.end(),
                    [&](const graph::GraphNode& n){ return n.id == s.id; });
                if (!dup) nodes.push_back(s);
            }
        }

        json results = json::array();
        for (auto& n : nodes) {
            if (!kind.empty() && n.kind != kind) continue;
            json row = {
                {"path",   n.path},
                {"kind",   n.kind},
                {"lang",   n.lang},
            };
            if (!n.symbol_name.empty()) row["symbol"] = n.symbol_name;
            if (!n.signature.empty())   row["signature"] = n.signature;
            if (n.line_start > 0)       row["line"] = n.line_start;
            if (!n.context.empty())     row["context"] = n.context.substr(0, 200);
            results.push_back(row);
            if (results.size() >= (size_t)limit) break;
        }

        return {
            {"query",   query},
            {"count",   results.size()},
            {"results", results},
        };
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_code_search", CodeSearchTool);

}  // namespace icmg::mcp
