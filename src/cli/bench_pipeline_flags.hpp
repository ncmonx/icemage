// 2026-10-31: bench-recall --dataset experimental-pipeline flags.
// Kept out of bench_recall_cmd.cpp (already ~390 lines).
//
//   --pipeline v2       preset: --semantic --fusion rrf --enrich --chunk 90
//                       --user-view --temporal 0.3 --group 0.3 --mmr 0.8
//                       (+ --neighbours 1 when granularity is turn)
//   --fusion blend|rrf  --enrich  --neighbours N  --chunk N  --user-view
//   --temporal B  --group B  --mmr L
// Individual flags override the preset, so stages can be ablated one by one.
#pragma once
#include "../imem/bench_dataset_run.hpp"

#include <string>
#include <vector>

namespace icmg::cli {

// Fills opt from args. Returns "" on success, else an error message.
std::string parseBenchPipelineFlags(const std::vector<std::string>& args,
                                    bool turnGranularity,
                                    imem::BenchRunOptions& opt);

// Usage lines appended to `bench-recall --help`.
const char* benchPipelineUsage();

// 2026-11-03: defaults applied after flag parsing, gated on which ONNX models
// are actually present (no model -> that stage stays off, behaviour unchanged):
//   rerank   -> --rerank 20 --rerank-chunk 150   (opt out: --no-rerank / --rerank 0)
//   semantic -> --semantic --alpha 0.3           (opt out: --no-semantic)
// Explicit flags / presets always win.
struct BenchModels {
    bool rerank = false;   // cross-encoder (BenchRerankPool) available
    bool embed  = false;   // embedder (BenchEmbedPool) available
};
void applyBenchDefaults(const std::vector<std::string>& args, const BenchModels& models,
                        imem::BenchRunOptions& opt);

}  // namespace icmg::cli
