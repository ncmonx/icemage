// v1.78.1: caveman → sayless flag-path auto-migration.
// Pure + header-only for unit tests. Called by `icmg init --force`.
//
// Migration mapping (project + global):
//   .icmg/caveman.flag  -> .icmg/sayless.flag
//   .icmg/caveman.off   -> .icmg/sayless.off
//
// Rules:
//   - Idempotent: re-running after success is a no-op (returns 0).
//   - Non-clobbering: if destination already exists, leave source untouched
//     (user is already on the new path; preserve current state).
//   - Returns count of files migrated this call.

#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace icmg::cli {

inline int migrateCavemanToSayless(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    const std::vector<std::pair<std::string, std::string>> pairs = {
        {".icmg/caveman.flag", ".icmg/sayless.flag"},
        {".icmg/caveman.off",  ".icmg/sayless.off" },
    };
    int migrated = 0;
    std::error_code ec;
    for (const auto& [from_rel, to_rel] : pairs) {
        fs::path from = root / from_rel;
        fs::path to   = root / to_rel;
        if (!fs::exists(from, ec)) continue;
        if (fs::exists(to, ec))    continue;   // non-clobber
        fs::create_directories(to.parent_path(), ec);
        fs::rename(from, to, ec);
        if (!ec) ++migrated;
    }
    return migrated;
}

}  // namespace icmg::cli
