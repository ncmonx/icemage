// v1.58 F3: bloom filter for graph node-path negative lookups.
//
// "Is this path definitely NOT a graph node?" answered in O(1) without a
// SQL round-trip. A bloom filter has no false negatives: if maybeContains()
// returns false, the path is guaranteed absent. A "true" may be a false
// positive (caller then falls through to the real SQL lookup).
//
// Safety: a negative answer is only trustworthy when the filter was built
// from the authoritative node set this process (built_ flag). Before build,
// maybeContains() conservatively returns true (forces SQL).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace icmg::graph {

class NodeBloom {
public:
    // Size the filter for an expected element count. m = next_pow2(10*n)
    // bits → ~1% FP at full load with k=7. n==0 uses a small default.
    void reset(std::size_t expected_n);

    void add(const std::string& key);

    // True = possibly present (caller must verify via SQL).
    // False = definitely absent (only when built()).
    bool maybeContains(const std::string& key) const;

    // Has the filter been populated from the authoritative set?
    bool built() const { return built_; }
    void markBuilt() { built_ = true; }

    std::size_t bitCount()  const { return bits_.size() * 64; }
    std::size_t addedCount() const { return added_; }

private:
    static constexpr int kHashes = 7;
    std::vector<uint64_t> bits_;   // bit array packed in 64-bit words
    uint64_t mask_ = 0;            // bit-index mask (size is power of two)
    std::size_t added_ = 0;
    bool built_ = false;

    void setBit(uint64_t idx);
    bool getBit(uint64_t idx) const;
};

}  // namespace icmg::graph
