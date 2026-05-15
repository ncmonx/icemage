#pragma once
// v1.1.1 — Resident-service auto-activation helper.
// Extracted from cli::service_cmd::doInstall() so that init / update / install
// flows can register the logon-trigger task without re-spawning icmg.
//
// All entry points are idempotent: re-running on an already-installed system
// re-writes the VBS, re-creates the schtasks entry (/F), and removes any
// legacy per-project autopilot schtasks left over from pre-v1.1.0.

#include <string>

namespace icmg::core {

// Install the resident service:
//   1) Write %USERPROFILE%/.icmg/service-launcher.vbs (hidden wscript shim).
//   2) Register Windows logon-trigger task "icmg-service" (/F overwrite).
//
// Skipped (returns true, err empty) when env ICMG_SKIP_SERVICE=1 is set
// or when running on POSIX (systemd/launchd path is out of scope for v1.1.1).
//
// Returns true on success. On failure, sets *err_out (when non-null) to a
// short reason. Helper never throws.
bool installResidentService(std::string* err_out);

// Remove legacy per-project autopilot Windows scheduled tasks created by
// pre-v1.1.0 versions of `icmg backup/maintain/mirror/sentinel/shadow-upgrade
// install --schedule`. Names matched by prefix:
//   icmg-backup-<hash>, icmg-maintain-<hash>, icmg-mirror-<hash>,
//   icmg-sentinel-<hash>, icmg-shadow-upgrade<suffix>
//
// Returns the count of tasks successfully deleted. Idempotent: a second call
// after a successful cleanup returns 0. POSIX: returns 0.
int cleanupLegacySchtasks();

} // namespace icmg::core
