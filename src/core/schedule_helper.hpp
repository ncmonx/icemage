// Phase 78: shared scheduler helper.
//
// Bulletproof auto-on for backup/mirror/maintain/sentinel/shadow-upgrade/cron.
// Bypasses 5 known schtasks bugs by:
//   1. Writing a .cmd wrapper file (single-arg /TR — no quote nesting)
//   2. Prepending MSYS_NO_PATHCONV=1 (defeats bash path-conv on /Create)
//   3. Self-elevating via PowerShell Start-Process -Verb RunAs when denied
//   4. Translating interval to correct /SC schedule type (DAILY/HOURLY/MINUTE)
//   5. Checking exit code + reporting real stderr (was empty due to merge_stderr)
//
// POSIX path uses crontab as before; no helper needed.

#pragma once
#include <string>

namespace icmg::core {

struct ScheduleSpec {
    std::string task_name;     // e.g. "icmg-backup-<hash>"
    std::string wrapper_path;  // absolute path to .cmd helper file
    int minutes;               // schedule interval
    std::string label;         // friendly name for logs ("backup", "sentinel"...)
};

// Compute stable per-project-per-user task name suffix (8 hex chars).
// Hashes project path + current username so concurrent users on the same
// server sharing a project path get distinct task names and don't overwrite
// each other's scheduled tasks.
std::string icmgTaskHash(const std::string& project_path);

// Write a Windows .cmd wrapper file to absolute_path.
// Wrapper contains: @echo off, cd /d <cwd>, echo timestamp banner, run <icmg_cmd>.
// Returns true on success. Creates parent dir if missing.
bool writeWrapperCmd(const std::string& wrapper_path,
                     const std::string& project_root,
                     const std::string& icmg_cmd,
                     const std::string& log_relpath = ".icmg\\sched\\sched.log");

// Register Windows scheduled task. Auto-elevates via PowerShell if denied.
// Returns 0 on success, non-zero on failure.
// On failure: prints manual setup instructions to stderr.
int registerWindowsSchedule(const ScheduleSpec& spec);

// Remove Windows scheduled task. Idempotent.
int unregisterWindowsSchedule(const std::string& task_name);

// Query Windows scheduled task. Returns true if exists + status string.
bool queryWindowsSchedule(const std::string& task_name, std::string* status_out = nullptr);

// v1.6.0: remove legacy per-project schtasks (backup/maintain/mirror/sentinel/
// shadow-upgrade-<hash>). Replaced by global cron_jobs iterator in icmg-service.
// Returns count of tasks deleted. POSIX: no-op (always returns 0).
int sweepLegacySchtasks();

} // namespace icmg::core
