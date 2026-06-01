#pragma once
// Phase 34: WordPiece tokenizer (BERT base-uncased convention).
// Pure C++; no ONNX/Python deps. Testable in isolation.
//
// Compatible with sentence-transformers/all-MiniLM-L6-v2 vocab.txt — same
// tokens used by Python sidecar so ONNX backend produces parity vectors.
//
// Limitations: english uncased only; basic Unicode (no full NFD normalization).
// For multilingual, swap to SentencePiece in Phase 36+.
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace icmg::embed {

class WordPieceTokenizer {
public:
    // Special token ids matching BERT defaults.
    static constexpr int64_t PAD_ID = 0;
    static constexpr int64_t UNK_ID = 100;
    static constexpr int64_t CLS_ID = 101;
    static constexpr int64_t SEP_ID = 102;

    struct Tokens {
        std::vector<int64_t> input_ids;
        std::vector<int64_t> attention_mask;
    };

    // Loads vocab.txt (one token per line, line index = token id).
    // Returns false on missing file. Sets sane defaults for special tokens
    // even if vocab incomplete.
    bool loadVocab(const std::string& path);

    // True after successful loadVocab().
    bool ready() const { return !vocab_.empty(); }

    // Encode text -> [CLS] + tokens + [SEP], padded/truncated to max_len.
    // attention_mask = 1 for real tokens, 0 for padding.
    Tokens encode(const std::string& text, int max_len = 256) const;

    // Sub-utilities exposed for tests.
    static std::string toLower(const std::string& s);
    static std::vector<std::string> splitWhitespacePunct(const std::string& s);
    std::vector<int64_t> wordpieceSplit(const std::string& word) const;

    int64_t lookupId(const std::string& token) const;
    size_t  vocabSize() const { return vocab_.size(); }

private:
    std::unordered_map<std::string, int64_t> vocab_;   // token -> id
};

} // namespace icmg::embed
