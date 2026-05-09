// Phase 40 T5: `icmg_compress` MCP tool — exposes Phase 39 compressor over MCP
// so Claude (or any MCP client) can compress its own dynamic context mid-turn.
//
// Input:  {text: string, aggressive?: bool, threshold?: int}
// Output: {compressed, tok_in, tok_out, saved_pct, glossary, hash, skipped, skip_reason}

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../compress/compressor.hpp"
#include "../../compress/glossary_store.hpp"

namespace icmg::mcp {

class CompressTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_compress"; }
    std::string description() const override {
        return "Semantic prompt compression with reversible glossary. "
               "Cuts dynamic-context tokens 30-60% on logs, diffs, dumps. "
               "Lossless by default; aggressive=true adds boilerplate filler-strip. "
               "Skips automatically below threshold or on source-code content.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"text",       "string",  "Text to compress",                                 true},
            {"aggressive", "boolean", "Strip boilerplate filler (lossy)",                false},
            {"threshold",  "integer", "Skip if est-tokens < this (default 8000)",         false},
            {"kind",       "string",  "Content kind hint (.log, .md, .cs, ...)",          false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "text", 5'000'000);  // 5MB cap
    }

    json callImpl(const json& args, core::Db& db) override {
        std::string text = getStr(args, "text");
        bool aggressive  = false;
        if (args.contains("aggressive") && args["aggressive"].is_boolean())
            aggressive = args["aggressive"].get<bool>();
        int threshold = 8000;
        if (args.contains("threshold") && args["threshold"].is_number_integer()) {
            threshold = args["threshold"].get<int>();
        }
        std::string kind = getStr(args, "kind");

        compress::CompressOptions opts;
        opts.mode          = aggressive ? compress::Mode::Aggressive
                                        : compress::Mode::Lossless;
        opts.threshold_tok = threshold;
        compress::Compressor c(opts);
        auto r = c.compress(text, kind);

        // Phase 54: write telemetry so dashboard reflects MCP-driven compression.
        try {
            compress::GlossaryStore store(db);
            if (!r.skipped) store.save(r.content_hash, r.glossary);
            store.recordTelemetry("compress", r.bytes_in, r.bytes_out,
                                   r.tok_in, r.tok_out, r.elapsed_ms,
                                   r.skipped ? "skipped"
                                             : (aggressive ? "aggressive" : "lossless"));
        } catch (...) {}

        json glossary = json::object();
        for (auto& kv : r.glossary) glossary[kv.first] = kv.second;

        int saved_pct = r.tok_in > 0 ? (100 - (100 * r.tok_out / r.tok_in)) : 0;
        return {
            {"compressed",   r.text},
            {"tok_in",       r.tok_in},
            {"tok_out",      r.tok_out},
            {"saved_pct",    saved_pct},
            {"bytes_in",     r.bytes_in},
            {"bytes_out",    r.bytes_out},
            {"elapsed_ms",   r.elapsed_ms},
            {"hash",         r.content_hash},
            {"skipped",      r.skipped},
            {"skip_reason",  r.skip_reason},
            {"glossary",     glossary},
            {"mode",         aggressive ? "aggressive" : "lossless"}
        };
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_compress", CompressTool);

} // namespace icmg::mcp
