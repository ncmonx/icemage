// token_counter.cpp — char-class weighted heuristic token estimator.
//
// Exact Claude token counts are impossible offline (Anthropic does not publish
// its tokenizer). This is an *estimate*. It improves on a uniform bytes/4 rule
// by weighting character classes the way BPE tokenizers actually behave:
// punctuation/symbols and digits pack into tokens more densely than letters,
// so code (punct-heavy) costs more tokens per byte than prose. Whitespace is
// absorbed (it rarely forms standalone tokens at this granularity).
#include "token_counter.hpp"
#include <cctype>
#include <cmath>

namespace icmg::core {

size_t estimateTokens(const std::string& text) {
    if (text.empty()) return 0;

    // Per-class char counts. Divisors = approximate chars-per-token for each
    // class, calibrated toward GPT-family BPE density (letters ~4/tok subword
    // merges; digits ~2.5; punct ~2; non-ASCII UTF-8 bytes ~2 -> CJK chars,
    // 3 bytes, land near 1.5 tokens each).
    double letters = 0, digits = 0, punct = 0, other = 0;
    for (unsigned char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;  // whitespace absorbed
        if (c < 128 && std::isalpha(c))      letters += 1.0;
        else if (c < 128 && std::isdigit(c)) digits  += 1.0;
        else if (c < 128 && std::ispunct(c)) punct   += 1.0;
        else                                 other   += 1.0;            // non-ASCII (UTF-8 multibyte)
    }

    double tokens = letters / 4.0 + digits / 2.5 + punct / 2.0 + other / 2.0;
    size_t result = static_cast<size_t>(std::ceil(tokens));

    // Any non-whitespace content is at least one token.
    if (result == 0 && (letters + digits + punct + other) > 0) result = 1;
    return result;
}

} // namespace icmg::core
