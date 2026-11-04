#pragma once
// 2026-10-31: cross-encoder reranker interface (bench-recall --rerank).
// A cross-encoder reads (query, doc) TOGETHER and emits one relevance logit,
// unlike the bi-encoder Embedder which embeds each side alone. Slower (one
// forward pass per pair) but much sharper on the final top-N.
//
// Backend: ONNX ms-marco-MiniLM-L-6-v2 (same BERT vocab as all-MiniLM).
// Factory returns nullptr when ONNX is not compiled in or the model is
// missing -- callers MUST keep the incoming order in that case.
#include <memory>
#include <string>
#include <vector>

namespace icmg::embed {

class Reranker {
public:
    virtual ~Reranker() = default;
    virtual std::string model() const = 0;
    // One logit per doc (higher = more relevant); empty on failure.
    virtual std::vector<float> score(const std::string& query,
                                     const std::vector<std::string>& docs) = 0;
};

// Model:  ICMG_RERANK_MODEL env, else ~/.icmg/embed/ms-marco-MiniLM-L-6-v2.onnx
// Vocab:  ICMG_RERANK_VOCAB env, else ~/.icmg/embed/ms-marco-vocab.txt,
//         else ~/.icmg/embed/vocab.txt (identical BERT uncased vocab).
std::unique_ptr<Reranker> makeReranker();

}  // namespace icmg::embed
