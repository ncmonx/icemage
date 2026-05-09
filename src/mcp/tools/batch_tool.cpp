// Phase 50 T4: icmg_batch MCP tool — Anthropic Batch API spec emitter.
// Bulk operations get 50% discount via Batch API.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class BatchTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_batch"; }
    std::string description() const override {
        return "Emit Anthropic Batch API request JSON spec from list of tasks. "
               "User pipes spec to /v1/messages/batches for 50% bulk discount. "
               "Pre-injects no-think/concise/caveman directives if requested.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"tasks",       "array",   "List of task strings",                  true},
            {"model",       "string",  "Default claude-sonnet-4-5",             false},
            {"max_tokens",  "integer", "Default 2000",                          false},
            {"directive",   "string",  "no-think | concise | caveman | none",   false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        if (!args.contains("tasks") || !args["tasks"].is_array() || args["tasks"].empty()) {
            throw McpError(-32602, "tasks must be non-empty array of strings");
        }
    }

    json callImpl(const json& args, core::Db& /*db*/) override {
        std::vector<std::string> tasks;
        for (auto& t : args["tasks"]) {
            if (t.is_string()) tasks.push_back(t.get<std::string>());
        }
        std::string model       = getStr(args, "model");
        int max_tokens          = args.contains("max_tokens") && args["max_tokens"].is_number_integer()
                                   ? args["max_tokens"].get<int>() : 2000;
        std::string directive   = getStr(args, "directive");

        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" batch";
        for (auto& t : tasks) {
            cmd += " --task \"" + escapeShell(t) + "\"";
        }
        cmd += " --max-tokens " + std::to_string(max_tokens);
        if (!model.empty())     cmd += " --model " + model;
        if (directive == "no-think")  cmd += " --no-think";
        if (directive == "concise")   cmd += " --concise";
        if (directive == "caveman")   cmd += " --caveman";

        auto res = core::safeExecShell(cmd, false, 30000);
        try {
            auto j = json::parse(res.out);
            return {
                {"spec",      j},
                {"exit_code", res.exit_code}
            };
        } catch (...) {
            return {
                {"exit_code", res.exit_code},
                {"stdout",    res.out},
                {"stderr",    res.err}
            };
        }
    }

private:
    static std::string escapeShell(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_batch", BatchTool);

} // namespace icmg::mcp
