// v1.78.2 Phase A: RAM-brain persist foundation.
// Pure helpers: scope hash (FNV-1a 64-bit of project DB path) + persist enable check.
// Phase B will add writeThrough; Phase C will add hydrate.
//
// Hash choice: FNV-1a 64-bit inline. No external dep. xxh64 considered but
// not vendored; FNV-1a sufficient for non-crypto bucket purpose.

#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <string_view>

namespace icmg::core {

// FNV-1a 64-bit, hex-encoded 16 chars. Deterministic, no allocs in hot path.
// Used as composite-PK column in recall_cache_persist so cross-project keys
// can never collide.
inline std::string scopeHash(std::string_view path) {
    uint64_t h = 0xCBF29CE484222325ULL;  // FNV offset basis (64-bit)
    for (unsigned char c : path) {
        h ^= c;
        h *= 0x100000001B3ULL;            // FNV prime (64-bit)
    }
    static const char hexchars[] = "0123456789abcdef";
    char buf[16];
    for (int i = 15; i >= 0; --i) {
        buf[i] = hexchars[h & 0xF];
        h >>= 4;
    }
    return std::string(buf, 16);
}

// Persist enabled when env ICMG_RECALL_CACHE_PERSIST != "0".
// Default ON (v1.78.2 chose opt-out semantics).
inline bool persistEnabled() {
    const char* v = std::getenv("ICMG_RECALL_CACHE_PERSIST");
    if (!v) return true;             // unset → default ON
    if (v[0] == '0' && v[1] == '\0') return false;
    return true;
}

// Persist OFF marker file (per-project): `.icmg/cache-persist.off`.
// Returns true if marker exists at the given project root. Env still wins
// over the on-disk marker — env=0 always wins, env=1 forces ON.
//
// Combined effective check (production):
//   if (env says OFF)            → OFF
//   else if (env says ON)        → ON
//   else (env unset, default ON) → check on-disk marker (OFF if present)
inline bool persistEnabledForRoot(const std::string& project_root) {
    const char* v = std::getenv("ICMG_RECALL_CACHE_PERSIST");
    if (v) {
        if (v[0] == '0' && v[1] == '\0') return false;
        return true;
    }
    // Env unset → consult on-disk marker.
    std::string marker = project_root.empty()
                             ? std::string(".icmg/cache-persist.off")
                             : project_root + "/.icmg/cache-persist.off";
    // Minimal stat — avoid <filesystem> here to keep header light.
    if (FILE* f = std::fopen(marker.c_str(), "rb")) {
        std::fclose(f);
        return false;
    }
    return true;
}

}  // namespace icmg::core
