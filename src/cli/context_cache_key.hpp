#pragma once
// Cache key for `icmg context` output (core::ToolCallCache).
//
// #290: the key used to list a handful of flags by hand, so any flag missing
// from that list (--head/--tail, --skeleton, --symbols-only, --compress-*, ...)
// replayed a cached read made WITHOUT it -- `icmg context f --head 3` printed
// the whole file. Every argument shapes the output, so the key covers the full
// argv plus the file's identity (path, mtime, size).
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace icmg::cli {

inline std::string contextCacheKey(const std::string& resolved_file, std::time_t mtime,
                                   std::uintmax_t size, const std::vector<std::string>& args) {
    std::string key = resolved_file + "|mtime=" + std::to_string(mtime)
                    + "|sz=" + std::to_string(size) + "|argv=";
    for (const auto& a : args) { key += a; key += '\x1f'; }
    return key;
}

}  // namespace icmg::cli
