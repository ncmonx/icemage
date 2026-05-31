// M8 T2: query fingerprint dedup — skip recall on identical query within session.
// Fingerprint = djb2(query) as uint32 hex string (7 chars). Lightweight.
#include "../test_main.hpp"
#include <string>
#include <cstdint>

// Fingerprint: djb2 → 8-char hex string. Non-crypto, session-stable.
inline std::string queryFingerprint(const std::string& q) {
    uint32_t h = 5381;
    for (unsigned char c : q) h = ((h << 5) + h) ^ c;
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08x", h);
    return std::string(buf);
}

TEST("fingerprint: identical queries yield same fingerprint") {
    ASSERT_EQ(queryFingerprint("linker error fix"), queryFingerprint("linker error fix"));
}

TEST("fingerprint: different queries yield different fingerprints") {
    ASSERT_TRUE(queryFingerprint("linker error") != queryFingerprint("build fail"));
    ASSERT_TRUE(queryFingerprint("") != queryFingerprint("a"));
}

TEST("fingerprint: output is 8-char hex") {
    auto fp = queryFingerprint("some query");
    ASSERT_EQ((int)fp.size(), 8);
    for (char c : fp) {
        ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST("fingerprint: usable for dedup cache key") {
    // Simulate: if fingerprint matches last query, skip recall
    std::string last_fp;
    auto shouldSkip = [&](const std::string& q) -> bool {
        auto fp = queryFingerprint(q);
        if (fp == last_fp) return true;
        last_fp = fp;
        return false;
    };
    ASSERT_FALSE(shouldSkip("linker error"));  // first query — run
    ASSERT_TRUE(shouldSkip("linker error"));   // same — skip
    ASSERT_FALSE(shouldSkip("build fail"));    // different — run
}
