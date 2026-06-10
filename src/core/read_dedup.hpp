#pragma once
// Read dedup: when `icmg context <file>` is a cache HIT, the model has already
// been shown this exact body this session -- the context cache key includes the
// file's mtime+size, so any on-disk edit would have MISSED the cache. Re-emitting
// the full body costs tokens for content the model already holds. So on a hit we
// emit a compact stub instead, with an explicit escape hatch (--full / --no-cache).
//
// Pure + header-only so the decision + stub text are unit-testable in isolation;
// the cache lookup itself stays in the context command.
#include <string>
#include <cstddef>

namespace icmg::core {

// Stub only when the cached body is big enough to be worth skipping AND the
// caller did not force a full re-emit. Small bodies emit in full (a stub would
// not save meaningfully and just adds a round-trip).
inline bool shouldStubContext(std::size_t cachedBytes, bool forceFull,
                              std::size_t minBytes = 400) {
    return !forceFull && cachedBytes >= minBytes;
}

// Compact stub for a context cache HIT (file unchanged since last shown).
inline std::string contextSeenStub(const std::string& file, std::size_t cachedBytes) {
    std::string s = "[icmg context: ";
    s += file;
    s += " unchanged -- already shown this session (~";
    s += std::to_string(cachedBytes);
    s += " B body skipped). Re-run with --full or --no-cache to re-emit.]\n";
    return s;
}

}  // namespace icmg::core
