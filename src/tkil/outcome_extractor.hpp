// v1.56 T1 Stage 4: outcome-only mode.
//
// For "fire-and-forget" commands (gh release, git push, curl -o, cmake
// --build), AI only needs the final result line — URL, ref-update, byte
// count, or executable path. outcomeOnly() detects eligible cmdlines and
// reduces the raw output to that outcome, while preserving any error /
// fatal / failure lines verbatim.
//
// Non-eligible cmdlines return input unchanged. Used as Stage 4 of the
// Tkil Ultra pipeline; runs after dedup + pattern collapse.

#pragma once

#include <string>

namespace icmg::tkil {

// Returns true if the cmdline is on the outcome-only allowlist.
// Currently: gh release {create,upload,edit}, git push, curl -o/-O,
// cmake --build, ninja, msbuild.
bool outcomeEligible(const std::string& cmdline);

// If `cmdline` is outcome-eligible, returns a heavily-reduced view of
// `input` containing only the outcome line(s) + any error/fatal lines.
// Otherwise returns input unchanged.
std::string outcomeOnly(const std::string& input, const std::string& cmdline);

}  // namespace icmg::tkil
