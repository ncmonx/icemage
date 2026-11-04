#pragma once
// 2026-11-03: BM25 "word" tokenizer, the DEFAULT since 2026-11-03 (approved by
// kak Cahyo). ICMG_BM25_TOKENIZER=legacy restores the old whitespace tokenizer.
// What it does: splits on every non-alnum ASCII byte, lowercases, and drops
// English stopwords. Bytes >= 0x80 count as word bytes, so UTF-8 words
// (Indonesian, CJK) are kept whole.
// Why: a pure-python ablation on LongMemEval_s (user-session unit, n=470)
// lifted any@5 from .928 to .955, past ICM's plain BM25 at .946.
#include <string>
#include <vector>

namespace icmg::imem {

// Env var + values. Unset / "word" / anything else = word; "legacy" = old.
inline constexpr const char* kBm25TokenizerEnv    = "ICMG_BM25_TOKENIZER";
inline constexpr const char* kBm25TokenizerWord   = "word";
inline constexpr const char* kBm25TokenizerLegacy = "legacy";

// True unless the env var asks for the legacy tokenizer.
bool bm25WordTokenizerFromEnv();

// Word tokenizer as described above. Pure function.
std::vector<std::string> wordTokenize(const std::string& text);

// True if `lowerToken` is in the English stopword list.
bool isBm25Stopword(const std::string& lowerToken);

} // namespace icmg::imem
