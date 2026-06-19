#include "output_gate.hpp"
#include "compressor.hpp"
#include "../core/output_cap.hpp"

namespace icmg::compress {

GateResult gateOutput(const std::string& full, std::size_t byte_budget) {
    GateResult r;
    r.bytes_in = (int)full.size();

    // Pass-through: already within budget (or trivially empty).
    if (full.size() <= byte_budget) {
        r.text = full;
        r.bytes_out = (int)full.size();
        return r;
    }

    // Try lossless compression first — keeps ALL content (reversible glossary),
    // unlike a truncating cap. force threshold to 0 so it always attempts.
    std::string candidate = full;
    bool did_compress = false;
    try {
        CompressOptions opts;
        opts.mode = Mode::Lossless;
        opts.threshold_tok = 0;       // never skip on size heuristic here
        Compressor c(opts);
        CompressResult cr = c.compress(full);
        // Only accept if it genuinely shrank the payload.
        if (!cr.skipped && !cr.text.empty() && cr.text.size() < full.size()) {
            candidate = cr.text;
            did_compress = true;
        }
    } catch (...) {
        candidate = full;             // compression failure -> fall through to cap
        did_compress = false;
    }

    // If compression alone brought us within budget, emit it (nothing lost).
    if (did_compress && candidate.size() <= byte_budget) {
        r.text = candidate;
        r.compressed = true;
        r.bytes_out = (int)candidate.size();
        return r;
    }

    // Still over budget (or compression didn't help): truncate + spill the
    // best candidate we have (compressed if it shrank, else the original).
    std::string spill;
    std::string capped = core::capOutput(candidate, byte_budget, spill);
    r.text       = capped;
    r.compressed = did_compress;       // honest: report if we compressed first
    r.capped     = true;
    r.spill_path = spill;
    r.bytes_out  = (int)capped.size();
    return r;
}

} // namespace icmg::compress
