#pragma once
// Phase 4: adapt a registered WASM extractor to graph::BaseExtractor, plus a
// PURE priority-arbitration helper (WASM vs built-in). No wasmtime at parse
// time; the adapter defers to runWasmExtractor() only inside extract().
#include "../graph/extractor/base_extractor.hpp"
#include "wasm_extractor.hpp"
#include "wasm_runtime.hpp"
#include <string>
#include <vector>

namespace icmg::wasm {

// Pure: given a language, the set of registered WASM extractors, and whether a
// built-in extractor exists for that language (with its implicit priority),
// decide whether the WASM extractor should win. A WASM extractor overrides the
// built-in only when its priority is strictly greater than the built-in's.
// Built-in extractors have implicit priority 0 (a WASM module must declare
// priority >= 1 to override; priority 0 defers to the built-in / fills a gap).
inline bool wasmExtractorWins(const WasmExtractor& wasm, bool builtinExists,
                              int builtinPriority = 0) {
    if (!builtinExists) return true;            // fills a gap -> always used
    return wasm.priority > builtinPriority;     // override only if higher
}

// Find the WASM extractor registered for `language` (exact match), if any.
// Returns index into `all`, or -1. Among ties, highest priority wins.
inline int selectWasmExtractor(const std::vector<WasmExtractor>& all,
                               const std::string& language) {
    int best = -1;
    for (int i = 0; i < (int)all.size(); ++i) {
        if (all[i].language != language) continue;
        if (best < 0 || all[i].priority > all[best].priority) best = i;
    }
    return best;
}

// Pure: which registered WASM extractor's language should a file extension map
// to? Returns the language, or "" if no registered extractor claims `ext`.
// Among extractors sharing an extension, the highest-priority one wins.
inline std::string wasmLangForExtension(const std::vector<WasmExtractor>& all,
                                        const std::string& ext) {
    int best = -1;
    for (int i = 0; i < (int)all.size(); ++i)
        for (const auto& x : all[i].extensions)
            if (x == ext && (best < 0 || all[i].priority > all[best].priority))
                best = i;
    return best < 0 ? std::string() : all[best].language;
}

// Pure: pick the WASM extractor to USE for `language`, honouring arbitration
// against a built-in. Returns index into `all`, or -1 (defer to built-in /
// none). Combines selectWasmExtractor + wasmExtractorWins.
inline int pickWasmExtractor(const std::vector<WasmExtractor>& all,
                             const std::string& language, bool builtinExists,
                             int builtinPriority = 0) {
    int idx = selectWasmExtractor(all, language);
    if (idx < 0) return -1;
    return wasmExtractorWins(all[idx], builtinExists, builtinPriority) ? idx : -1;
}

// BaseExtractor adapter: runs a WASM module under extractor-v1 and maps its
// JSON output to ExtractResult. Fail-open: on any runtime error returns an empty
// result (never throws) so a broken skill degrades to "no symbols", not a crash.
class WasmExtractorAdapter : public graph::BaseExtractor {
public:
    explicit WasmExtractorAdapter(WasmExtractor def) : def_(std::move(def)) {}

    graph::ExtractResult extract(const std::string& /*path*/,
                                 const std::string& content) override {
        graph::ExtractResult r;
        std::string rerr;
        runWasmExtractor(def_, content, WasmLimits{}, r, rerr);  // fail-open
        return r;
    }

    std::vector<std::string> extensions() const override { return def_.extensions; }

    const WasmExtractor& def() const { return def_; }

private:
    WasmExtractor def_;
};

} // namespace icmg::wasm
