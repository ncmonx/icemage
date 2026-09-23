// 2026-09-23: recall confidence tier -- IDE-B from the landscape scan
// (MemCalib, arXiv 2609.24259). Injected memory without a calibration cue gets
// over-trusted (stale hit steers the turn) or under-trusted (real answer
// ignored). One word per hit tells the model how hard to lean on it.
// Pure mapping, no DB/IO/LLM.
#pragma once

namespace icmg::imem {

// Map a BM25 recall score to a coarse confidence tier, relative to the
// injection floor (min_score = the threshold a hit must clear to be shown).
//   high : >= 3x floor -- lean on it
//   med  : >= 1.5x     -- probably right, verify if load-bearing
//   low  : cleared the floor only -- treat as a hint, not a fact
inline const char* confidenceTier(double score, double min_score) {
    if (min_score <= 0.0) return score > 0.0 ? "high" : "low";
    if (score >= 3.0 * min_score) return "high";
    if (score >= 1.5 * min_score) return "med";
    return "low";
}

}  // namespace icmg::imem
