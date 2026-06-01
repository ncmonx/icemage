// M8 T1: djb2 hash for LRU cache keys — faster than std::hash<string>.
// Verifies correctness + consistency, not raw speed (benchmarked separately).
#include "../test_main.hpp"
#include <string>
#include <unordered_map>

// djb2 hash: Daniel J. Bernstein. Non-crypto, ~100x faster than SHA256.
// Runtime-stable (no ASLR-based randomization unlike std::hash on some platforms).
struct Djb2Hash {
    std::size_t operator()(const std::string& s) const noexcept {
        std::size_t h = 5381;
        for (unsigned char c : s) h = ((h << 5) + h) ^ c;
        return h;
    }
};

TEST("djb2: identical strings produce same hash") {
    Djb2Hash h;
    ASSERT_EQ(h("recall:linker error"), h("recall:linker error"));
}

TEST("djb2: different strings produce different hashes (basic collision test)") {
    Djb2Hash h;
    ASSERT_TRUE(h("recall:linker") != h("recall:build"));
    ASSERT_TRUE(h("") != h("a"));
    ASSERT_TRUE(h("abc") != h("cba"));
}

TEST("djb2: usable as unordered_map key hasher") {
    std::unordered_map<std::string, int, Djb2Hash> m;
    m["recall:query one"] = 1;
    m["recall:query two"] = 2;
    ASSERT_EQ(m.at("recall:query one"), 1);
    ASSERT_EQ(m.at("recall:query two"), 2);
    ASSERT_EQ((int)m.size(), 2);
}

TEST("djb2: empty string hashes consistently") {
    Djb2Hash h;
    ASSERT_EQ(h(""), h(""));
    ASSERT_TRUE(h("") != h(" "));
}
