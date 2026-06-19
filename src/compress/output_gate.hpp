// Slice-2 of Adaptive Output Gate: gateOutput.
//
// The decision layer that links lossless compression into the output-budget
// path. Big tool outputs are expensive; capOutput() truncates (loses the
// middle). gateOutput() prefers a LOSSLESS compress (reversible glossary, the
// whole content survives) and only truncates as a last resort.
#pragma once
#include <string>

namespace icmg::compress {

struct GateResult {
    std::string text;                 // what to emit downstream
    bool        compressed = false;   // lossless compress was applied
    bool        capped     = false;   // truncated + spilled (last resort)
    int         bytes_in   = 0;       // original size (always)
    int         bytes_out  = 0;       // emitted size
    std::string spill_path;           // set when capped and spill succeeded
};

// Adaptive gate. Never throws.
//   under budget            -> pass through unchanged
//   over budget, compresses  -> emit compressed (compressed=true)
//   over budget, won't shrink-> capOutput truncate+spill (capped=true)
// When compression still exceeds budget it is additionally capped.
GateResult gateOutput(const std::string& full, std::size_t byte_budget);

} // namespace icmg::compress
