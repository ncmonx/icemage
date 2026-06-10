#pragma once
// Per-model API pricing registry (USD per 1M tokens, input / output).
//
// Cost estimates were hardcoded to the Sonnet rate ($3/$15); running on Opus
// ($15/$75) understates spend ~5x. This maps a model id (substring) to its
// list price so `icmg savings` (and other cost readouts) are honest per-model.
//
// IMPORTANT: list-prices are VOLATILE -- unlike context windows, vendors change
// them. These values are as-of 2026-06 and are a *default convenience*, NOT a
// guarantee: every caller keeps an explicit `--rate-input/--rate-output`
// override, which always wins. The default fallback is the Sonnet rate, so an
// unknown model behaves exactly as before (no regression).
//
// Pure + header-only so it is unit-testable in isolation.
#include <string>

namespace icmg::cli {

struct ModelRates { double in = 3.0, out = 15.0; };  // $/MTok; default = Sonnet

// Substring-match, first hit wins, most-specific first. Scoped to high-
// confidence tiers; everything else falls back to the Sonnet default.
inline ModelRates modelPricing(const std::string& model) {
    struct Entry { const char* needle; double in; double out; };
    static const Entry kTable[] = {
        // Anthropic (as-of 2026-06 list prices)
        {"opus-4",   15.00, 75.00},
        {"sonnet-4",  3.00, 15.00},
        {"haiku-4",   1.00,  5.00},
        // OpenAI
        {"gpt-4o",    2.50, 10.00},
        {"o1",       15.00, 60.00},
        // Google
        {"gemini",    1.25,  5.00},
    };
    for (const auto& e : kTable)
        if (model.find(e.needle) != std::string::npos) return {e.in, e.out};
    return ModelRates{};  // default = Sonnet 3/15 (no regression for unknown)
}

}  // namespace icmg::cli
