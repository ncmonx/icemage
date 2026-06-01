// Phase 50 T4: icmg_fetch MCP tool — URL download + content-aware reduction.
// 70-90% token saving on docs/API responses vs raw WebFetch.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/url_sanitize.hpp"
#include <cstdlib>
#include <filesystem>

namespace icmg::mcp {

class FetchTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_fetch"; }
    std::string description() const override {
        return "Download URL with content-aware reduction (HTML strips chrome, "
               "JSON schema-summarizes >5KB, PDF via sidecar, binary metadata-only). "
               "Cached per URL+ETag (1h TTL). Saves 70-90% input tokens vs raw WebFetch.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"url",        "string",  "URL to fetch (http/https only)",        true},
            {"kind",       "string",  "Force kind: html|json|pdf|text|binary", false},
            {"max_bytes",  "integer", "Output cap (default 8192)",             false},
            {"refresh",    "boolean", "Bypass cache, re-download",             false},
            {"raw",        "boolean", "Skip reduction, return raw body",       false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        std::string url = getStr(args, "url");
        std::string reason;
        if (!core::validateUrl(url, reason)) {
            throw McpError(-32602, std::string("invalid url: ") + reason);
        }
    }

    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string url = getStr(args, "url");
        std::string kind = getStr(args, "kind");
        int max_bytes = args.contains("max_bytes") && args["max_bytes"].is_number_integer()
                         ? args["max_bytes"].get<int>() : 8192;
        bool refresh = args.contains("refresh") && args["refresh"].is_boolean()
                        ? args["refresh"].get<bool>() : false;
        bool raw     = args.contains("raw")     && args["raw"].is_boolean()
                        ? args["raw"].get<bool>() : false;

        // Shellout to icmg fetch (re-uses cache + reduction logic).
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" fetch \"" + url + "\""
                        + " --max-bytes " + std::to_string(max_bytes);
        if (!kind.empty()) cmd += " --kind " + kind;
        if (refresh) cmd += " --refresh";
        if (raw)     cmd += " --raw";
        auto res = core::safeExecShell(cmd, false, 60000);
        return {
            {"url",       url},
            {"exit_code", res.exit_code},
            {"body",      res.out},
            {"meta",      res.err}     // [fetch] kind=X N->M (P% saved) lives on stderr
        };
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_fetch", FetchTool);

} // namespace icmg::mcp
