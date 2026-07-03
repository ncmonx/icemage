// tests/mcp/test_tool_budget.cpp
// #2 tool-schema diet -- unit tests for the pure MCP tool budget analyzer.
#include "../test_main.hpp"
#include "../../src/mcp/tool_budget.hpp"

using namespace icmg::mcp;

// Empty set -> zeroed report, no divide-by-zero.
TEST("toolbudget: empty is safe") {
    auto rep = analyzeToolBudget({});
    ASSERT_EQ((int)rep.tools, 0);
    ASSERT_EQ((int)rep.total_tokens, 0);
    ASSERT_TRUE(rep.rows.empty());
}

// Per-tool char/token accounting: name + description + schema.
TEST("toolbudget: counts name + desc + schema chars") {
    std::vector<ToolSchemaInfo> tools = {
        {"a", "desc", "{\"x\":1}"},   // 1 + 4 + 7 = 12 chars -> 3 tokens
    };
    auto rep = analyzeToolBudget(tools);
    ASSERT_EQ((int)rep.rows[0].total_chars, 12);
    ASSERT_EQ((int)rep.rows[0].tokens, 3);
    ASSERT_EQ((int)rep.total_tokens, 3);
}

// Rows sorted by token cost descending.
TEST("toolbudget: rows sorted by tokens desc") {
    std::vector<ToolSchemaInfo> tools = {
        {"small", "x", "{}"},
        {"huge", std::string(400, 'y'), std::string(400, 'z')},
        {"mid", std::string(80, 'm'), "{}"},
    };
    auto rep = analyzeToolBudget(tools);
    ASSERT_EQ(rep.rows[0].name, std::string("huge"));
    ASSERT_EQ(rep.rows[2].name, std::string("small"));
}

// A disproportionately large tool is flagged verbose; typical ones are not.
TEST("toolbudget: oversized tool flagged as diet candidate") {
    std::vector<ToolSchemaInfo> tools = {
        {"t1", std::string(20, 'a'), "{}"},
        {"t2", std::string(20, 'a'), "{}"},
        {"t3", std::string(20, 'a'), "{}"},
        {"whale", std::string(2000, 'w'), std::string(2000, 'w')},
    };
    auto rep = analyzeToolBudget(tools, 1.5);
    // whale is row 0 (largest) and must be flagged; the small ones must not.
    ASSERT_TRUE(rep.rows[0].name == std::string("whale"));
    ASSERT_TRUE(rep.rows[0].verbose);
    ASSERT_FALSE(rep.rows[3].verbose);
}

// Aggregate totals across the set.
TEST("toolbudget: totals aggregate across tools") {
    std::vector<ToolSchemaInfo> tools = {
        {"aa", std::string(40, 'x'), "{}"},   // 2+40+2 = 44
        {"bb", std::string(40, 'y'), "{}"},   // 44
    };
    auto rep = analyzeToolBudget(tools);
    ASSERT_EQ((int)rep.total_chars, 88);
    ASSERT_EQ((int)rep.total_tokens, 22);
    ASSERT_EQ((int)rep.tools, 2);
}
