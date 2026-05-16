#pragma once
// token_counter.hpp — heuristic token estimator (no network, no external deps).
// Target: match tiktoken cl100k_base within ±20% for typical English/code content.
//
// Heuristic:
//   tokens ≈ ceil(non-whitespace chars / 4.0)
//           + punct/space transitions / 8   (word-boundary bonus)
//   clamp lower bound: max(1, tokens) when bytes > 0

#include <string>
#include <cstddef>

namespace icmg::core {

/// Returns an approximate token count for the given UTF-8 text.
/// Deterministic, cheap, no allocations beyond the input string.
size_t estimateTokens(const std::string& text);

} // namespace icmg::core
