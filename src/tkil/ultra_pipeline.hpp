// v1.56 T1: Tkil Ultra pipeline orchestrator.
//
// Chains Stages 2-5 (dedup -> pattern collapse -> outcome-only -> session
// glossary) on top of the existing per-cmd BaseFilter (Stage 1). Used by
// Tkil::runFiltered when `ultra` mode is on or when the auto-trigger
// heuristic fires (output > 5 KB AND dup-ratio > 0.4).

#pragma once

#include <string>

namespace icmg::tkil {

// Apply the full Ultra pipeline to `input` (which has already been through
// Stage 1, the per-cmd BaseFilter). `cmdline` is the original command line
// — used by Stage 3 (pattern profiles) and Stage 4 (outcome-only) to
// decide whether they apply.
//
// Process-wide SessionGlossary instance is used for Stage 5 so callers
// in the same process share token assignments (mimics what the future
// daemon will provide cross-call).
std::string applyUltraPipeline(const std::string& input,
                                const std::string& cmdline);

// Compute the duplication ratio of `text` over line granularity.
// Returns (lines - distinct_lines) / lines, or 0 when empty.
double duplicationRatio(const std::string& text);

// Returns true when the auto-trigger conditions are met:
//   output > 5 KB AND duplicationRatio > 0.4
bool autoTriggerUltra(const std::string& text);

}  // namespace icmg::tkil
