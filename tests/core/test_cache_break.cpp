// M8 T6: cache break detection via state hash.
// Technique from claude-code promptCacheBreakDetection.ts:
// hash system prompt + model config; if unchanged, skip recompute.
#include "../test_main.hpp"
#include <string>
#include <cstdint>

// djb2 pair hash: combine two strings into one hash for cache break detection.
inline uint32_t hashPair(const std::string& a, const std::string& b) noexcept {
    uint32_t h = 5381;
    for (unsigned char c : a) h = ((h << 5) + h) ^ c;
    h ^= 0xdeadbeef; // separator
    for (unsigned char c : b) h = ((h << 5) + h) ^ c;
    return h;
}

// CacheBreakDetector: tracks last-seen state hash; detects when it changes.
struct CacheBreakDetector {
    uint32_t last_hash = 0;
    bool     valid     = false;

    // Returns true if state changed (cache break detected).
    bool check(const std::string& system_prompt, const std::string& model) {
        uint32_t h = hashPair(system_prompt, model);
        if (!valid || h != last_hash) {
            last_hash = h;
            valid     = true;
            return true; // break
        }
        return false; // cache hit
    }

    void invalidate() { valid = false; }
};

TEST("cache_break: same state → no break") {
    CacheBreakDetector d;
    ASSERT_TRUE(d.check("prompt", "sonnet"));  // first call always breaks
    ASSERT_FALSE(d.check("prompt", "sonnet")); // same → hit
    ASSERT_FALSE(d.check("prompt", "sonnet")); // still same
}

TEST("cache_break: model change → break") {
    CacheBreakDetector d;
    d.check("prompt", "sonnet");
    ASSERT_TRUE(d.check("prompt", "opus")); // model changed
}

TEST("cache_break: prompt change → break") {
    CacheBreakDetector d;
    d.check("original prompt", "sonnet");
    ASSERT_TRUE(d.check("changed prompt", "sonnet")); // prompt changed
}

TEST("cache_break: invalidate forces next check to break") {
    CacheBreakDetector d;
    d.check("p", "m");
    d.invalidate();
    ASSERT_TRUE(d.check("p", "m")); // after invalidate, always breaks
}
