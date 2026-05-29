// v1.75.0 (#187): pure decision for whether to (re)run the Windows Defender
// process-exclusion step (Add-MpPreference -ExclusionProcess).
//
// Background: `icmg init` already cached success in
// %USERPROFILE%\.icmg\defender-excluded.flag and skipped on subsequent runs.
// `icmg update --apply` did NOT — it re-ran Add-MpPreference on every upgrade.
// Because the exclusion is keyed by process PATH (unchanged across atomic-
// rename upgrades), re-adding is a no-op for security but still spawns
// PowerShell + nudges Defender to enumerate/scan all logical volumes, which
// touches a `subst` alias (e.g. B:\) and pops the "insert disk in drive B:"
// dialog. Making the call idempotent (honor the flag) + opt-out removes it.
#pragma once

namespace icmg {
namespace cli {

// Returns true only when the Defender exclusion step should actually run.
//   flag_exists     : %USERPROFILE%\.icmg\defender-excluded.flag present
//   env_no_defender : ICMG_NO_DEFENDER set (any non-empty value)
//   arg_no_defender : --no-defender passed on the command line
// Opt-out (env or arg) always wins; otherwise skip when already excluded.
inline bool shouldRunDefenderExclusion(bool flag_exists,
                                       bool env_no_defender,
                                       bool arg_no_defender) {
    if (env_no_defender) return false;
    if (arg_no_defender) return false;
    if (flag_exists)     return false;
    return true;
}

}  // namespace cli
}  // namespace icmg
