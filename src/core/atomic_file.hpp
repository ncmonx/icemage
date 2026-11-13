#pragma once
// Validate-first, atomic file writes (issue #277).
//
// writeFileAtomic writes `content` to a sibling temp file and renames it over
// `target`, so a reader (or a crash mid-write) sees either the old file or the
// complete new one -- never a truncated/empty file. The directory must exist.
#include <filesystem>
#include <string>
#include <string_view>

namespace icmg::core {

// Strict UTF-8 check (RFC 3629): rejects overlongs, surrogates, > U+10FFFF,
// truncated sequences and stray continuation / cp1252 bytes.
bool isValidUtf8(std::string_view s);

// Returns false (and fills *err when non-null) on any failure; the original
// target is left untouched in that case.
bool writeFileAtomic(const std::filesystem::path& target, const std::string& content,
                     std::string* err = nullptr);

}  // namespace icmg::core
