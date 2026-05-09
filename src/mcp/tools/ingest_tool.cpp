// Phase 50 T4: icmg_ingest MCP tool — OCR images locally instead of vision call.
// 90%+ saving + 5x faster on text-heavy screenshots.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>
#include <filesystem>

namespace icmg::mcp {

class IngestTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_ingest"; }
    std::string description() const override {
        return "OCR an image file locally via Python sidecar (pytesseract). "
               "Returns extracted text + confidence + cache hit indicator. "
               "Saves 90%+ tokens vs vision call on text-heavy screenshots "
               "(code, terminal, error messages). Cache 7d per image hash.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"path",      "string",  "Local image file path",                    true},
            {"refresh",   "boolean", "Bypass cache, re-OCR",                     false},
            {"raw",       "boolean", "Skip OCR; return metadata only",            false},
            {"min_chars", "integer", "Below = vision-recommended note (def 30)",  false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        std::string p = getStr(args, "path");
        if (p.empty() || !std::filesystem::exists(p)) {
            throw McpError(-32602, std::string("file not found: ") + p);
        }
    }

    bool isMutating() const override { return true; }  // writes image_cache + telemetry

    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string path = getStr(args, "path");
        bool refresh = args.contains("refresh") && args["refresh"].is_boolean()
                        ? args["refresh"].get<bool>() : false;
        bool raw     = args.contains("raw")     && args["raw"].is_boolean()
                        ? args["raw"].get<bool>() : false;
        int min_chars = args.contains("min_chars") && args["min_chars"].is_number_integer()
                         ? args["min_chars"].get<int>() : 30;

        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" ingest \"" + path + "\" --json"
                        + " --min-chars " + std::to_string(min_chars);
        if (refresh) cmd += " --refresh";
        if (raw)     cmd += " --raw";
        auto res = core::safeExecShell(cmd, false, 60000);

        // Output is JSON line; parse into structured response.
        try {
            auto j = json::parse(res.out);
            j["exit_code"] = res.exit_code;
            return j;
        } catch (...) {
            return {
                {"path",      path},
                {"exit_code", res.exit_code},
                {"stdout",    res.out},
                {"stderr",    res.err}
            };
        }
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_ingest", IngestTool);

} // namespace icmg::mcp
