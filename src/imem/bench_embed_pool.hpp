// 2026-10-31: parallel embedding for bench-recall --dataset.
//
// The live path uses one cached ONNX session (single-threaded, mutex-guarded):
// ~13 embeds/s, so a full LongMemEval_s semantic run took 30-57 min. The
// bench owns its own pool of embedder instances (one ONNX session each) and a
// process-wide text -> vector cache (LongMemEval re-uses ~23% of sessions
// across instances). Bench-only: never used by live recall.
#pragma once
#include <string>
#include <vector>

namespace icmg::imem {

class BenchEmbedPool {
public:
    static BenchEmbedPool& instance();

    // False when no embedder backend exists (then embedMany returns empties).
    bool available();
    int dim();
    std::string model();

    // One vector per input text (empty vector on failure). Cached by text.
    std::vector<std::vector<float>> embedMany(const std::vector<std::string>& texts);
    std::vector<float> embedOne(const std::string& text);

    // Worker count; ICMG_BENCH_THREADS overrides (default: hw-2, clamp 1..8).
    int threads() const;

private:
    BenchEmbedPool() = default;
};

}  // namespace icmg::imem
