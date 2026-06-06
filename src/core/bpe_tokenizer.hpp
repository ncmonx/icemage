#pragma once
// bpe_tokenizer.hpp — tiktoken-style byte-pair-merge tokenizer (token COUNTING).
//
// Opt-in backend for *exact* GPT/OpenAI-family token counts (icmg is model-
// agnostic: many users run Copilot/Cursor/ChatGPT on cl100k/o200k). The default
// path stays the zero-dependency heuristic estimateTokens(); this is consulted
// only when ICMG_TOKENIZER=bpe-* AND a .tiktoken rank file is available. We only
// need COUNTS (not token ids) for savings, so this never builds an id vector.
#include <string>
#include <unordered_map>
#include <vector>
#include <cstddef>

namespace icmg::core {

class BpeTokenizer {
public:
    // cl100k-style pre-tokenizer (ASCII-faithful; Unicode approximated). Splits
    // text into chunks BEFORE byte-pair merge, replicating the tiktoken regex:
    //   contractions | optional-lead + letters | 1-3 digits | space?+punct+nl* | ws
    // Exposed as a pure static so it can be unit-tested against known splits.
    static std::vector<std::string> preTokenize(const std::string& text);

    // Load mergeable ranks from a tiktoken file: each line "<base64-bytes> <rank>".
    // Returns true on success (>=1 rank parsed).
    bool loadRanks(const std::string& path);
    bool ready() const { return !ranks_.empty(); }

    // Count tokens for ONE pre-token byte chunk via byte-pair merge: repeatedly
    // merge the lowest-rank adjacent pair until none remain; result = piece count.
    // Pure given the rank table. Empty -> 0.
    size_t mergeCount(const std::string& piece) const;

    // Count tokens for arbitrary text: pre-tokenize into chunks, sum mergeCount.
    size_t countTokens(const std::string& text) const;

    // Test seam: inject a single rank without a vocab file.
    void addRankForTest(const std::string& piece, int rank) { ranks_[piece] = rank; }

private:
    std::unordered_map<std::string, int> ranks_;
};

}  // namespace icmg::core
