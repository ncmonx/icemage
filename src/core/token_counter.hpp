#pragma once
// token_counter.hpp — token counting with a selectable backend.
//
// Default = char-class weighted heuristic (no network, no deps). Estimate only
// (letters ~4 chars/tok, digits ~2.5, punct ~2, non-ASCII ~2; whitespace
// absorbed) — Claude's tokenizer is not public, so this is an approximation.
// Opt-in exact backends (icmg is model-agnostic) selected via ICMG_TOKENIZER:
//   heuristic (default) | bpe-cl100k | bpe-o200k | anthropic-api
// A bpe-* backend needs a vocab file (downloaded on opt-in); if it is missing
// the count falls back to the heuristic.

#include <string>
#include <cstddef>

namespace icmg::core {

class BpeTokenizer;  // fwd

/// Char-class weighted heuristic token estimate. Deterministic, cheap.
size_t estimateTokens(const std::string& text);

enum class TokBackend { Heuristic, BpeCl100k, BpeO200k, AnthropicApi };

/// Parse the ICMG_TOKENIZER value to a backend. nullptr/unknown -> Heuristic.
TokBackend tokBackendFromEnv(const char* env);

/// Pure delegator: count tokens for `text` under `backend`. A bpe-* backend uses
/// `bpe` when non-null AND ready, else falls back to the heuristic. AnthropicApi
/// is not yet wired here -> heuristic. Testable without env/filesystem.
size_t countTokensWith(const std::string& text, TokBackend backend, const BpeTokenizer* bpe);

/// Backend-aware entry: reads ICMG_TOKENIZER, lazily loads + caches the bpe vocab
/// for bpe-* backends, and delegates. This is what call sites (Tkil/savings) use.
size_t countTokens(const std::string& text);

} // namespace icmg::core
