#pragma once
// Binary-swap helpers for `icmg update --apply` (issue #277 bug 1).
#include <filesystem>
#include <string>

namespace icmg::cli {

struct RollbackResult {
    bool ok = false;
    std::string message;  // human-readable reason when !ok
};

// Put `bak` back at `self`. Invariant: whatever happens, `self` exists on
// return (old binary on success, the current one on any failure). The current
// binary is parked at `<self>.swap` only while the restore is in flight.
RollbackResult rollbackSwap(const std::filesystem::path& self,
                            const std::filesystem::path& bak);

// Set mtime to now. A renamed .bak keeps the old exe's mtime, which made
// `doctor` (run by the post-upgrade self-test) archive it as a stale orphan.
void touchNow(const std::filesystem::path& p);

}  // namespace icmg::cli
