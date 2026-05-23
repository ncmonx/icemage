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

// v1.28.0: cached singleton accessor. Returns raw pointer to a process-
// local Embedder instance — lazy-init on first call, then reused. Use
// for hot paths (MemoryStore::recallSemantic) that previously paid 5-6s
// of ONNX cold-load per call. Returns nullptr if no backend is available;
// callers must null-check exactly like makeEmbedder().
// Thread-safety: init is guarded by std::call_once; embed() inherits the
// backend's thread-safety (ONNX session is single-thread safe per icmg
// usage). Not for callers that need fresh state between calls.
Embedder* cachedEmbedder();

// Pack/unpack helpers for SQLite BLOB storage.
std::vector<uint8_t> packVec(const std::vector<float>& v);
std::vector<float>   unpackVec(const std::vector<uint8_t>& blob, int dim);

// Cosine similarity. Returns 0 on dim mismatch / empty input.
float cosine(const std::vector<float>& a, const std::vector<float>& b);

// FNV1a hash (matches scanner / symbol_extractor) for body_hash staleness.
std::string fnv1a64(const std::string& s);

} // namespace icmg::embed
