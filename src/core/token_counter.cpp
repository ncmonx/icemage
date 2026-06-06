// token_counter.cpp — char-class weighted heuristic token estimator.
//
// Exact Claude token counts are impossible offline (Anthropic does not publish
// its tokenizer). This is an *estimate*. It improves on a uniform bytes/4 rule
// by weighting character classes the way BPE tokenizers actually behave:
// punctuation/symbols and digits pack into tokens more densely than letters,
// so code (punct-heavy) costs more tokens per byte than prose. Whitespace is
// absorbed (it rarely forms standalone tokens at this granularity).
#include "token_counter.hpp"
#include "bpe_tokenizer.hpp"
#include "path_utils.hpp"   // icmgGlobalDir
#include <cctype>
#include <cmath>
#include <cstdlib>          // getenv

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

TokBackend tokBackendFromEnv(const char* env) {
    if (!env) return TokBackend::Heuristic;
    std::string v(env);
    if (v == "bpe-cl100k")    return TokBackend::BpeCl100k;
    if (v == "bpe-o200k")     return TokBackend::BpeO200k;
    if (v == "anthropic-api") return TokBackend::AnthropicApi;
    return TokBackend::Heuristic;   // "heuristic", empty, or unknown -> safe default
}

size_t countTokensWith(const std::string& text, TokBackend backend, const BpeTokenizer* bpe) {
    switch (backend) {
        case TokBackend::BpeCl100k:
        case TokBackend::BpeO200k:
            if (bpe && bpe->ready()) return bpe->countTokens(text);
            return estimateTokens(text);            // vocab missing -> heuristic fallback
        case TokBackend::AnthropicApi:               // network backend not wired here yet
        case TokBackend::Heuristic:
        default:
            return estimateTokens(text);
    }
}

size_t countTokens(const std::string& text) {
    TokBackend backend = tokBackendFromEnv(std::getenv("ICMG_TOKENIZER"));
    if (backend == TokBackend::Heuristic || backend == TokBackend::AnthropicApi)
        return estimateTokens(text);

    // bpe-*: lazily load + cache the vocab once per process (1.7MB file).
    static BpeTokenizer* g_bpe = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::string fname = (backend == TokBackend::BpeO200k)
                              ? "o200k_base.tiktoken" : "cl100k_base.tiktoken";
        std::string path = icmgGlobalDir() + "/tokenizer/" + fname;
        auto* t = new BpeTokenizer();
        if (t->loadRanks(path)) g_bpe = t; else delete t;
    }
    return countTokensWith(text, backend, g_bpe);
}

} // namespace icmg::core
