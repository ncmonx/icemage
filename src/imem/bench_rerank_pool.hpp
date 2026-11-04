// 2026-10-31: parallel cross-encoder scoring for bench-recall --rerank.
// Mirrors BenchEmbedPool: N independent ONNX sessions (ICMG_BENCH_THREADS),
// one query's candidate docs split across them. Bench-only.
#pragma once
#include <string>
#include <vector>

namespace icmg::imem {

class BenchRerankPool {
public:
    static BenchRerankPool& instance();

    // False when ONNX is absent or the cross-encoder model is missing.
    bool available();
    std::string model();

    // One logit per doc; empty when unavailable or any pair failed.
    // maxWorkers > 0 caps the ONNX sessions used (live recall: 1 -- each extra
    // session reloads the model, which dwarfs scoring ~20 short pairs).
    std::vector<float> score(const std::string& query, const std::vector<std::string>& docs,
                             int maxWorkers = 0);

private:
    BenchRerankPool() = default;
};

}  // namespace icmg::imem
