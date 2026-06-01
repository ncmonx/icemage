// v1.21.4 (FB3 #1): icmg_bench_recall MCP tool.
// Runs the bench-recall scenario harness; returns pass/fail summary.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class BenchRecallTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_bench_recall"; }
    std::string description() const override {
        return "Run recall scenario harness against the project memory store. "
               "Reads scenarios from `bench/recall_scenarios.txt` by default "
               "(format: query|expect-csv|top-k). Returns per-scenario PASS/FAIL "
               "+ summary. Exit 0 = all passed.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"file",     "string",  "Path to scenario file (default bench/recall_scenarios.txt)", false},
            {"semantic", "boolean", "Use semantic blend (default false = pure BM25)", false},
        };
    }

protected:
    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" bench-recall --json";
        if (args.contains("file") && args["file"].is_string())
            cmd += " --file \"" + args["file"].get<std::string>() + "\"";
        if (args.value("semantic", false)) cmd += " --semantic";
        auto res = core::safeExecShell(cmd, false, 60000);
        json out;
        try { out["scenarios"] = json::parse(res.out); }
        catch (...) { out["stdout"] = res.out; }
        out["exit_code"] = res.exit_code;
        return out;
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_bench_recall", BenchRecallTool);

} // namespace icmg::mcp
