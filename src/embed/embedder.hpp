#pragma once
// Phase 23: text-to-vector embedder interface.
// Two backends planned: Python sidecar (default) + ONNX (tier-2, future).
// Factory returns nullptr if no backend available — callers MUST gracefully
// fall back to BM25-only paths.
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace icmg::embed {

class Embedder {
public:
    virtual ~Embedder() = default;
    virtual bool        available() const = 0;
    virtual int         dim() const = 0;                       // 384 for MiniLM
    virtual std::string model() const = 0;
    virtual std::vector<float> embed(const std::string& text) = 0;
};

// Factory: tries Python sidecar; returns nullptr when unavailable.
std::unique_ptr<Embedder> makeEmbedder();

// Pack/unpack helpers for SQLite BLOB storage.
std::vector<uint8_t> packVec(const std::vector<float>& v);
std::vector<float>   unpackVec(const std::vector<uint8_t>& blob, int dim);

// Cosine similarity. Returns 0 on dim mismatch / empty input.
float cosine(const std::vector<float>& a, const std::vector<float>& b);

// FNV1a hash (matches scanner / symbol_extractor) for body_hash staleness.
std::string fnv1a64(const std::string& s);

} // namespace icmg::embed
