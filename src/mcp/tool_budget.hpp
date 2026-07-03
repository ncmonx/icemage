// src/mcp/tool_budget.hpp
// #2 tool-schema diet: measure the per-turn token cost of MCP tool schemas.
//
// Every registered MCP tool contributes its name + description + parameter
// schema to the system prompt on EVERY turn. With ~34 tools this is a fixed
// recurring cost. This pure analyzer scores each tool's schema size so rarely
// used or over-described tools can be trimmed.
//
// Pure + header-only: takes already-serialised (name, description, schema_json)
// triples so it needs neither the registry nor nlohmann/json to be tested.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace icmg::mcp {

struct ToolSchemaInfo {
    std::string name;
    std::string description;
    std::string schema_json;   // serialised JSON Schema for the params
};

struct ToolBudgetRow {
    std::string name;
    int64_t     desc_chars   = 0;
    int64_t     schema_chars = 0;
    int64_t     total_chars  = 0;   // name + description + schema
    int64_t     tokens       = 0;   // ~total_chars / 4
    bool        verbose      = false;  // flagged as a diet candidate
};

struct ToolBudgetReport {
    int64_t tools        = 0;
    int64_t total_chars  = 0;
    int64_t total_tokens = 0;
    double  avg_tokens   = 0.0;
    std::vector<ToolBudgetRow> rows;   // sorted by tokens desc
};

inline int64_t budgetTokens(int64_t chars) { return chars / 4; }

// Analyse a set of tool schemas. A tool is a "diet candidate" (verbose=true)
// when its token cost exceeds `verbose_factor` x the average -- i.e. it is
// disproportionately large relative to its peers.
inline ToolBudgetReport analyzeToolBudget(const std::vector<ToolSchemaInfo>& tools,
                                          double verbose_factor = 1.5) {
    ToolBudgetReport rep;
    rep.tools = (int64_t)tools.size();
    if (tools.empty()) return rep;

    for (const auto& t : tools) {
        ToolBudgetRow r;
        r.name         = t.name;
        r.desc_chars   = (int64_t)t.description.size();
        r.schema_chars = (int64_t)t.schema_json.size();
        r.total_chars  = (int64_t)t.name.size() + r.desc_chars + r.schema_chars;
        r.tokens       = budgetTokens(r.total_chars);
        rep.total_chars += r.total_chars;
        rep.rows.push_back(std::move(r));
    }
    rep.total_tokens = budgetTokens(rep.total_chars);
    rep.avg_tokens   = (double)rep.total_tokens / (double)rep.tools;

    double threshold = rep.avg_tokens * verbose_factor;
    for (auto& r : rep.rows) r.verbose = (double)r.tokens > threshold;

    std::sort(rep.rows.begin(), rep.rows.end(),
        [](const ToolBudgetRow& a, const ToolBudgetRow& b) {
            if (a.tokens != b.tokens) return a.tokens > b.tokens;
            return a.name < b.name;
        });
    return rep;
}

} // namespace icmg::mcp
