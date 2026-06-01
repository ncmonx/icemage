// Phase 67 T29: MCP wrappers for new icmg cmds (fail/correction/distill/
// receipt/entropy/tool-budget/shorten/context-budget). Subprocess pattern
// matches sync/fetch/savings tools — single source of truth in CLI.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

namespace {
std::string icmgBin() {
    return std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
}

json runIcmg(const std::string& tail, int timeout_ms = 30000) {
    std::string cmd = "\"" + icmgBin() + "\" " + tail;
    auto res = core::safeExecShell(cmd, false, timeout_ms);
    json out;
    out["exit_code"] = res.exit_code;
    // Try parse JSON if --json was in tail; otherwise raw stdout.
    if (tail.find("--json") != std::string::npos) {
        try { return json::parse(res.out); } catch (...) {}
    }
    out["stdout"] = res.out;
    if (!res.err.empty()) out["stderr"] = res.err;
    return out;
}
} // namespace

// ---------- icmg_fail ----------
class FailTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_fail"; }
    std::string description() const override {
        return "Anti-pattern memory. action=store|recall|list. "
               "store records failed approaches per task; recall surfaces "
               "matching past failures so Claude avoids repeating.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"action",   "string", "store|recall|list",          true},
            {"task",     "string", "Task description (store/recall)", false},
            {"approach", "string", "Failed approach (store)",    false},
            {"reason",   "string", "Why it failed (store)",      false},
            {"limit",    "integer","Recall/list limit",          false},
        };
    }
protected:
    bool isMutating() const override { return true; }  // store writes memory
    void validateArgs(const json& args) override { requireStr(args, "action", 20); }
    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string action = getStr(args, "action");
        std::string tail = "fail " + action;
        if (action == "store") {
            tail += " \"" + getStr(args, "task") + "\""
                  + " \"" + getStr(args, "approach") + "\""
                  + " \"" + getStr(args, "reason") + "\"";
        } else if (action == "recall") {
            tail += " \"" + getStr(args, "task") + "\"";
            int lim = getInt(args, "limit", 5);
            tail += " --limit " + std::to_string(lim);
        } else if (action == "list") {
            int lim = getInt(args, "limit", 20);
            tail += " --limit " + std::to_string(lim);
        }
        return runIcmg(tail);
    }
};

// ---------- icmg_distill ----------
class DistillTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_distill"; }
    std::string description() const override {
        return "Auto-extract decisions/facts from text into memory. "
               "action=auto|session|show. auto reads `text` arg, extracts "
               "Decision/Fix/IMPORTANT lines, stores as memory_node.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"action", "string", "auto|session|show", true},
            {"text",   "string", "Text body (auto/session)", false},
            {"limit",  "integer","List limit (show)", false},
        };
    }
protected:
    bool isMutating() const override { return true; }
    void validateArgs(const json& args) override { requireStr(args, "action", 20); }
    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string action = getStr(args, "action");
        if (action == "show") {
            int lim = getInt(args, "limit", 20);
            return runIcmg("distill show --limit " + std::to_string(lim));
        }
        // auto / session — pipe text via stdin
        std::string text = getStr(args, "text");
        if (text.empty()) return {{"error", "text required for auto/session"}};
        // Use shell echo + pipe.
        std::string esc = text;
        // Escape backslashes and double-quotes for shell interpolation.
        std::string out;
        for (char c : esc) {
            if (c == '\\' || c == '"') out += '\\';
            out += c;
        }
        std::string cmd = "echo \"" + out + "\" | \"" + icmgBin() + "\" distill " + action;
        auto res = core::safeExecShell(cmd, false, 30000);
        return {{"exit_code", res.exit_code}, {"stdout", res.out}, {"stderr", res.err}};
    }
};

// ---------- icmg_correction ----------
class CorrectionTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_correction"; }
    std::string description() const override {
        return "Track corrections to AI-emitted code. action=recall|list. "
               "(capture is hook-fed via PostToolUse:Edit, not exposed here.)";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"action", "string", "recall|list",         true},
            {"task",   "string", "Task keywords (recall)", false},
            {"limit",  "integer","Result limit",        false},
        };
    }
protected:
    void validateArgs(const json& args) override { requireStr(args, "action", 20); }
    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string action = getStr(args, "action");
        std::string tail = "correction " + action;
        if (action == "recall") {
            tail += " \"" + getStr(args, "task") + "\""
                  + " --limit " + std::to_string(getInt(args, "limit", 5));
        } else if (action == "list") {
            tail += " --limit " + std::to_string(getInt(args, "limit", 20));
        }
        return runIcmg(tail);
    }
};

// ---------- icmg_receipt ----------
class ReceiptTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_receipt"; }
    std::string description() const override {
        return "Token-receipt ledger from icmg pack/context calls. "
               "action=show|total|by-file. show=last N rows, total=per-source "
               "aggregate, by-file=per-label aggregate (find hot files).";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"action", "string",  "show|total|by-file", true},
            {"last",   "integer", "show: last N rows", false},
            {"window", "string",  "total: window like 7d", false},
            {"top",    "integer", "by-file: top N",    false},
        };
    }
protected:
    void validateArgs(const json& args) override { requireStr(args, "action", 20); }
    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string action = getStr(args, "action");
        std::string tail = "receipt " + action;
        if (action == "show")    tail += " --last " + std::to_string(getInt(args, "last", 20));
        else if (action == "total") tail += " --window " + getStr(args, "window", "7d");
        else if (action == "by-file") tail += " --top " + std::to_string(getInt(args, "top", 20));
        return runIcmg(tail);
    }
};

// ---------- icmg_entropy ----------
class EntropyTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_entropy"; }
    std::string description() const override {
        return "File-edit entropy from git log (hot files = bad pack candidates). "
               "action=show. Run `icmg entropy scan` from CLI first.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"top", "integer", "Top N files (default 20)", false},
        };
    }
protected:
    json callImpl(const json& args, core::Db& /*db*/) override {
        return runIcmg("entropy show --top " + std::to_string(getInt(args, "top", 20)));
    }
};

// ---------- icmg_tool_budget ----------
class ToolBudgetTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_tool_budget"; }
    std::string description() const override {
        return "Per-turn tool-call gate. action=set|get|status|reset.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"action", "string", "set|get|status|reset", true},
            {"limit",  "integer","set: tool calls/turn", false},
        };
    }
protected:
    bool isMutating() const override { return true; }  // set writes flag file
    void validateArgs(const json& args) override { requireStr(args, "action", 20); }
    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string action = getStr(args, "action");
        std::string tail = "tool-budget " + action;
        if (action == "set") tail += " " + std::to_string(getInt(args, "limit", 20));
        return runIcmg(tail);
    }
};

// ---------- icmg_shorten ----------
class ShortenTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_shorten"; }
    std::string description() const override {
        return "Strip filler/politeness/redundancy from a verbose prompt. "
               "Returns shortened text + size delta.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"text",       "string",  "Verbose input text", true},
            {"aggressive", "boolean", "Also strip parentheticals", false},
        };
    }
protected:
    void validateArgs(const json& args) override { requireStr(args, "text", 100000); }
    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string text = getStr(args, "text");
        bool aggressive = args.contains("aggressive") && args["aggressive"].is_boolean()
                            ? args["aggressive"].get<bool>() : false;
        // pipe text via stdin
        std::string esc;
        for (char c : text) {
            if (c == '\\' || c == '"') esc += '\\';
            esc += c;
        }
        std::string cmd = "echo \"" + esc + "\" | \"" + icmgBin() + "\" shorten"
                        + (aggressive ? " --aggressive" : "") + " --diff";
        auto res = core::safeExecShell(cmd, false, 10000);
        return {
            {"exit_code", res.exit_code},
            {"shortened", res.out},
            {"original_size", (int)text.size()},
            {"shortened_size", (int)res.out.size()},
            {"stderr", res.err}
        };
    }
};

// ---------- icmg_context_budget ----------
class ContextBudgetTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_context_budget"; }
    std::string description() const override {
        return "Real Claude session token usage from latest transcript. "
               "Covers ALL sources (assistant/thinking/tool-input/tool-output) "
               "vs icmg_savings which counts only icmg-instrumented ops.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"top",        "integer", "Top N largest entries (default 10)", false},
            {"transcript", "string",  "Override path to .jsonl",            false},
        };
    }
protected:
    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string tail = "context-budget --json --top "
                         + std::to_string(getInt(args, "top", 10));
        std::string path = getStr(args, "transcript");
        if (!path.empty()) tail += " --transcript \"" + path + "\"";
        return runIcmg(tail);
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_fail",            FailTool);
ICMG_REGISTER_MCP_TOOL("icmg_distill",         DistillTool);
ICMG_REGISTER_MCP_TOOL("icmg_correction",      CorrectionTool);
ICMG_REGISTER_MCP_TOOL("icmg_receipt",         ReceiptTool);
ICMG_REGISTER_MCP_TOOL("icmg_entropy",         EntropyTool);
ICMG_REGISTER_MCP_TOOL("icmg_tool_budget",     ToolBudgetTool);
ICMG_REGISTER_MCP_TOOL("icmg_shorten",         ShortenTool);
ICMG_REGISTER_MCP_TOOL("icmg_context_budget",  ContextBudgetTool);

} // namespace icmg::mcp
