#pragma once
// token_counter.hpp — heuristic token estimator (no network, no external deps).
// Estimate only — Claude's tokenizer is not public. Char-class weighted:
// letters ~4 chars/tok, digits ~2.5, punct ~2, non-ASCII ~2; whitespace absorbed.
// Beats uniform bytes/4 by accounting for code (punct-heavy) density.

#include <string>
#include <cstddef>

namespace icmg::core {

/// Returns an approximate token count for the given UTF-8 text.
/// Deterministic, cheap, no allocations beyond the input string.
size_t estimateTokens(const std::string& text);

} // namespace icmg::core
