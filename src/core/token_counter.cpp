// token_counter.cpp — heuristic token estimator implementation.
#include "token_counter.hpp"
#include <cctype>
#include <cmath>

namespace icmg::core {

size_t estimateTokens(const std::string& text) {
    if (text.empty()) return 0;

    size_t non_ws = 0;
    size_t transitions = 0;  // punct/space boundary count

    bool prev_ws = true;  // treat start-of-string as whitespace boundary
    for (unsigned char c : text) {
        bool is_ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (!is_ws) ++non_ws;
        // Count transitions: whitespace<->non-whitespace or alphanum<->punct
        if (is_ws != prev_ws) ++transitions;
        prev_ws = is_ws;
    }

    // Baseline: non-whitespace chars / 4 (matches GPT tokenizer empirically)
    double base = std::ceil(static_cast<double>(non_ws) / 4.0);

    // Small bonus per word boundary (punct/space transitions)
    double bonus = static_cast<double>(transitions) / 8.0;

    size_t result = static_cast<size_t>(base + bonus);

    // Clamp: if there are bytes, return at least 1
    if (result == 0 && !text.empty() && non_ws > 0) result = 1;

    return result;
}

} // namespace icmg::core
