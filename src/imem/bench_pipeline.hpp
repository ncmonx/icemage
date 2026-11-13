// 2026-10-31: experimental retrieval pipeline for bench-recall --dataset.
// BM25 (MemoryStore::recallAsOf over the throwaway brain) + bench-owned dense
// (BenchEmbedPool, multi-vector max-sim) -> fusion -> temporal / group prior
// -> MMR. Every stage is opt-in via BenchRunOptions so it can be ablated.
#pragma once
#include "bench_dataset.hpp"
#include "bench_dataset_run.hpp"
#include "bench_metrics.hpp"

#include <vector>

namespace icmg::imem {

class MemoryStore;

// Doc text after --enrich / --neighbours (pure; exposed for tests).
std::vector<BenchDoc> enrichBenchDocs(const std::vector<BenchDoc>& docs,
                                      bool dateLabel, int neighbours);

// Ranked doc ids for every question of a corpus already stored in `store`
// (node id -> doc index in `docs`). *embedded = vectors computed.
std::vector<std::vector<std::string>>
runBenchPipeline(const std::vector<BenchDoc>& docs,
                 const std::vector<int64_t>& nodeIds,
                 const std::vector<BenchQuestion>& questions,
                 MemoryStore& store, const BenchRunOptions& opt,
                 int maxK, int* embedded);

// --rerank-gate accounting, process-wide (corpora may run on several threads).
struct RerankGateStats {
    long long considered = 0;   // questions that reached the rerank stage
    long long skipped    = 0;   // ...of which the gate skipped the cross-encoder
};
RerankGateStats benchRerankGateStats();
void resetBenchRerankGateStats();

}  // namespace icmg::imem
