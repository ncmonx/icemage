#pragma once
// Hook refresh that respects user edits (issue #281).
//
// `icmg update --apply` refreshes project hooks via `init --install-hooks
// --force`. init used to overwrite every hook file unconditionally, so local
// edits (e.g. an rg fallback added to .claude/grep-hook.js) were silently
// reset on every upgrade. Now init records the hash of every hook it writes in
// <.claude>/.icmg-hook-manifest.json and decides per file:
//   - content still matches the recorded hash -> pristine, upgrade it;
//   - content differs from the record          -> user edit, keep it and drop
//                                                 the new template beside it as
//                                                 <file>.icmg-new;
//   - no record yet (legacy) and content differs -> back up to
//                                                 .claude/.icmg-hook-backup/,
//                                                 then write the template;
//   - `init --overwrite-hooks`                   -> back up, then write.
#include <filesystem>
#include <map>
#include <string>

namespace icmg::cli {

inline constexpr const char* kHookManifestName   = ".icmg-hook-manifest.json";
inline constexpr const char* kHookNewSuffix      = ".icmg-new";
inline constexpr const char* kHookBackupDir      = ".icmg-hook-backup";
inline constexpr const char* kOverwriteHooksFlag = "--overwrite-hooks";

enum class HookAction { Create, Unchanged, Upgrade, KeepUser, BackupAndWrite };

// FNV-1a 64-bit, lowercase hex (16 chars). Stable across platforms/builds.
std::string hookHash(const std::string& content);

// `recorded_hash` empty = no manifest record for this file.
HookAction decideHookAction(bool exists, const std::string& current,
                            const std::string& tmpl,
                            const std::string& recorded_hash, bool overwrite);

// Manifest keys are paths relative to the .claude dir, '/'-separated.
std::map<std::string, std::string> loadHookManifest(const std::filesystem::path& claude_dir);
bool saveHookManifest(const std::filesystem::path& claude_dir,
                      const std::map<std::string, std::string>& m);

}  // namespace icmg::cli
