#pragma once
#include <string>

namespace icmg::core {

// Resolve symlinks + normalize separators. Throws if path doesn't exist
// and require_exists=true.
std::string canonicalize(const std::string& path, bool require_exists = false);

// Make a path absolute WITHOUT resolving symlinks (fs::absolute semantics).
// Never throws: the throwing std::filesystem::absolute reaches PathCch on some
// Windows Server SKUs and raises filesystem_error err126 ("specified module
// could not be found"), crashing path-arg commands. Falls back to a purely-
// lexical absolute on failure. Empty input -> empty output.
std::string absolutePath(const std::string& path);

// True if 'path' is inside 'root' (both canonicalized).
bool isWithinRoot(const std::string& path, const std::string& root);

// v1.65 S1: validate an UNTRUSTED file-path argument (e.g. from an MCP tool
// call) before it is interpolated into a shell command or opened. Rejects
// shell-metacharacters (" ' ; | & $ ` \n \r < > * and control chars) and
// parent-dir traversal segments ("..") that could escape the project root.
// Returns true only when the path is safe to use. Empty path -> false.
bool isSafeToolPath(const std::string& path);

// True if file at 'path' is a SQLite database (magic bytes "SQLite format 3").
bool isSQLiteFile(const std::string& path);

// Expand leading "~" to home directory.
std::string expandTilde(const std::string& path);

// Get home directory cross-platform.
std::string homeDir();

// Get icmg global config dir (~/.icmg or %APPDATA%\icmg).
std::string icmgGlobalDir();

// Cross-platform self executable path.
// Windows: GetModuleFileNameA. Linux: /proc/self/exe via canonical.
// macOS: _NSGetExecutablePath + canonical. Returns "" on failure.
std::string selfExePath();

// v1.56 hotfix: persona DB lives at icmg.exe directory, NOT per-user APPDATA.
// Rationale: one icmg install → one persona shared across all Win users on
// that host. Previous APPDATA placement made persona invisible to other
// users / SYSTEM service runs. Returns <exe-dir>/icmg-persona.db on success,
// empty on selfExePath failure (caller falls back to globalDbPath).
std::string personaDbPath();

// v1.56 hotfix: relax Win ACL on a file or directory so BUILTIN\Users + SYSTEM
// gain full control. Called after every new DB or mirror creation so other
// Win users / SYSTEM service launches can read the same data. No-op on
// Linux/macOS. Returns true on success or non-Win platforms; false if the
// Win API call failed (caller may warn but should not abort).
bool relaxAclEveryone(const std::string& path);

} // namespace icmg::core
