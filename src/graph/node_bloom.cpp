// v1.58 F3: bloom filter for graph node-path negative lookups — impl.

#include "node_bloom.hpp"

namespace icmg::graph {

namespace {

inline uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

// splitmix64 — second independent hash for double-hashing scheme.
inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

inline uint64_t nextPow2(uint64_t v) {
    if (v < 1024) return 1024;
    --v;
    v |= v >> 1; v |= v >> 2; v |= v >> 4;
    v |= v >> 8; v |= v >> 16; v |= v >> 32;
    return v + 1;
}

}  // namespace

void NodeBloom::reset(std::size_t expected_n) {
    uint64_t m = nextPow2(static_cast<uint64_t>(expected_n) * 10);
    mask_ = m - 1;
    bits_.assign(static_cast<std::size_t>(m / 64), 0ULL);
    added_ = 0;
    built_ = false;
}

void NodeBloom::setBit(uint64_t idx) {
    bits_[static_cast<std::size_t>(idx >> 6)] |= (1ULL << (idx & 63));
}

bool NodeBloom::getBit(uint64_t idx) const {
    return (bits_[static_cast<std::size_t>(idx >> 6)] >> (idx & 63)) & 1ULL;
}

void NodeBloom::add(const std::string& key) {
    if (bits_.empty()) reset(1024);
    uint64_t h1 = fnv1a64(key);
    uint64_t h2 = splitmix64(h1);
    for (int i = 0; i < kHashes; ++i) {
        uint64_t idx = (h1 + static_cast<uint64_t>(i) * h2) & mask_;
        setBit(idx);
    }
    ++added_;
}

bool NodeBloom::maybeContains(const std::string& key) const {
    // Not built yet → cannot trust a negative; force SQL fallthrough.
    if (!built_ || bits_.empty()) return true;
    uint64_t h1 = fnv1a64(key);
    uint64_t h2 = splitmix64(h1);
    for (int i = 0; i < kHashes; ++i) {
        uint64_t idx = (h1 + static_cast<uint64_t>(i) * h2) & mask_;
        if (!getBit(idx)) return false;   // definitely absent
    }
    return true;                          // possibly present
}

}  // namespace icmg::graph
