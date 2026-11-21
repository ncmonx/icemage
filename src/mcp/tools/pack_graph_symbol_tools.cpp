// #292 item 1: the two CLI capabilities an MCP client still had to shell out
// for -- `icmg pack` (task-context bundle) and `icmg graph symbol` (where is X
// defined). Both are read-only, so they stay exposed under `--read-only`.
// Graph impact is already served by icmg_graph_query (blast_radius|who_calls).
#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../cli/base_command.hpp"
#include "../../graph/graph_store.hpp"
#include "../../core/memory_scope.hpp"   // #292 namespace on pack
#include <algorithm>
#include <iostream>
#include <mutex>
#include <sstream>

namespace icmg::mcp {

namespace {

// Bounds shared by schema text and clamps (one place).
constexpr int kPackDefaultBytes  = 4096;
constexpr int kPackMaxBytes      = 65536;
constexpr int kPackDefaultMemory = 5;
constexpr int kPackMaxMemory     = 20;
constexpr int kSymbolDefault     = 10;
constexpr int kSymbolMax         = 100;

// Runs a registered CLI command in-process and returns its stdout. stdout is
// the MCP JSON-RPC channel, so the swap is serialized and always restored.
std::string runCommandCaptured(const std::string& cmd, const std::vector<std::string>& argv,
                               int* rc) {
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    std::ostringstream out, err;
    auto* oldOut = std::cout.rdbuf(out.rdbuf());
    auto* oldErr = std::cerr.rdbuf(err.rdbuf());
    *rc = -1;
    try {
        auto c = core::Registry<cli::BaseCommand>::instance().create(cmd);
        if (c) *rc = c->run(argv);
    } catch (...) {
        *rc = -1;
    }
    std::cout.rdbuf(oldOut);
    std::cerr.rdbuf(oldErr);
    return out.str();
}

}  // namespace

class PackTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_pack"; }
    std::string description() const override {
        return "Task-context bundle for a task description: relevant memories, files and "
               "rules in one capped text block (same as `icmg pack`).";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"task",         "string",  "What you are about to do",                     true},
            {"zone",         "string",  "Limit to one zone",                             false},
            {"max_bytes",    "integer", "Output cap in bytes (default 4096, max 65536)", false},
            {"memory_limit", "integer", "Memory hits to include (default 5, max 20)",    false},
            {"namespace",    "string",  "Comma list of tenant namespaces the memories may come from", false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "task", 4000);
    }

    json callImpl(const json& args, core::Db&) override {
        const std::string task = getStr(args, "task");
        const int bytes = std::max(256, std::min(kPackMaxBytes,
                                                 getInt(args, "max_bytes", kPackDefaultBytes)));
        const int mem = std::max(1, std::min(kPackMaxMemory,
                                             getInt(args, "memory_limit", kPackDefaultMemory)));
        // --no-diff: an MCP client wants the whole bundle, not a delta against
        // whatever the last CLI pack happened to be. --raw: plain text.
        std::vector<std::string> argv = {task, "--max-bytes", std::to_string(bytes),
                                         "--memory-limit", std::to_string(mem),
                                         "--no-diff", "--raw", "--no-auto-zone"};
        const std::string zone = getStr(args, "zone");
        if (!zone.empty()) { argv.push_back("--zone"); argv.push_back(zone); }
        std::vector<std::string> ns;
        std::string err;
        if (!core::parseNamespaces(getStr(args, "namespace"), &ns, &err))
            throw McpError(-32602, err);
        core::NamespaceScope nsScope(ns);
        int rc = 0;
        std::string text = runCommandCaptured("pack", argv, &rc);
        if (rc != 0 && text.empty()) return {{"error", "pack failed (rc=" + std::to_string(rc) + ")"}};
        return {{"task", task}, {"pack", text}, {"bytes", (int)text.size()}};
    }
};

class GraphSymbolTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_graph_symbol"; }
    std::string description() const override {
        return "Find where a function/class/method is defined: file, kind, signature and "
               "line range (same as `icmg graph symbol`; suffix/case-insensitive match).";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"name",  "string",  "Symbol name (bare or qualified)",   true},
            {"limit", "integer", "Max results (default 10, max 100)", false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "name", 512);
    }

    json callImpl(const json& args, core::Db& db) override {
        const std::string sym = getStr(args, "name");
        const int limit = std::max(1, std::min(kSymbolMax, getInt(args, "limit", kSymbolDefault)));
        graph::GraphStore store(db);
        json arr = json::array();
        for (const auto& n : store.findSymbol(sym)) {
            if ((int)arr.size() >= limit) break;
            arr.push_back({{"symbol", n.symbol_name}, {"kind", n.kind}, {"path", n.path},
                           {"signature", n.signature},
                           {"line_start", n.line_start}, {"line_end", n.line_end}});
        }
        return {{"name", sym}, {"matches", arr}, {"count", (int)arr.size()}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_pack",         PackTool);
ICMG_REGISTER_MCP_TOOL("icmg_graph_symbol", GraphSymbolTool);

}  // namespace icmg::mcp
