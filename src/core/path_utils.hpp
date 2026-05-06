#pragma once
#include <string>

namespace icmg::core {

// Resolve symlinks + normalize separators. Throws if path doesn't exist
// and require_exists=true.
std::string canonicalize(const std::string& path, bool require_exists = false);

// True if 'path' is inside 'root' (both canonicalized).
bool isWithinRoot(const std::string& path, const std::string& root);

// True if file at 'path' is a SQLite database (magic bytes "SQLite format 3").
bool isSQLiteFile(const std::string& path);

// Expand leading "~" to home directory.
std::string expandTilde(const std::string& path);

// Get home directory cross-platform.
std::string homeDir();

// Get icmg global config dir (~/.icmg or %APPDATA%\icmg).
std::string icmgGlobalDir();

} // namespace icmg::core
