// 2026-10-29: run a BenchCorpus through the REAL icmg recall path on an
// isolated in-memory brain (never touches the user's project DB).
//   - every doc -> MemoryStore::store(force) with valid_from = session date
//     (force: Jaccard dedup must not merge look-alike turns = lost evidence)
//   - every question -> MemoryStore::recallAsOf(question, as_of, maxK)
//     (bi-temporal view: only docs dated <= the question date are visible;
//      side-effect free, so question order cannot bias later questions)
#pragma once
#include "bench_dataset.hpp"
#include "bench_metrics.hpp"
#include <string>
#include <vector>

namespace icmg::imem {

// 2026-10-29: --semantic = hybrid BM25+cosine (MemoryStore::recallSemanticAsOf).
// Every doc is embedded into the throwaway brain's embeddings table first.
struct BenchRunOptions {
    bool   semantic = false;
    double alpha    = 0.5;   // BM25 weight in the blend (1 = pure BM25)

    // 2026-10-31 experimental pipeline (bench_pipeline.cpp). Any of these set
    // -> the bench does its own BM25 + dense retrieval instead of
    // recallSemanticAsOf, so each stage can be measured in isolation.
    std::string fusion;          // "" (legacy) | blend | rrf
    bool   enrich      = false;  // prepend the session date label to every doc
    int    neighbours  = 0;      // turn docs: append +/-N same-session turns
    int    chunkWords  = 0;      // >0: multi-vector dense over word chunks (max-sim)
    bool   userView    = false;  // extra dense vectors over the user's own lines
    double temporal    = 0.0;    // >0: boost docs inside a parsed time window
    double groupBeta   = 0.0;    // >0: lift turns by their session's best score
    double mmrLambda   = 1.0;    // <1: MMR diversification of the top pool
    int    rerankTop   = 0;      // >0: cross-encoder rerank of the final top N
    int    rerankChunk = 0;      // >0: rerank on N-word passages, doc = max passage
    double prfBeta     = 0.0;    // >0: RM3 pseudo-relevance feedback, lexical interp. weight
    double coverLambda = 0.0;    // >0: coverage-greedy head (new query terms weight)
    double rerankGate  = 0.0;    // >0: skip rerank on top-1 margin >= this + full coverage

    // 2026-11-02: ignore question dates (every doc visible). LongMemEval_s has
    // 41/470 questions whose evidence is dated after question_date; the
    // as-of view hides it. Visibility only -- not a pipeline stage.
    bool   noAsOf      = false;

    // 2026-11-03: score abstention questions that still have evidence (ICM /
    // MemPalace report LongMemEval_s over all 500). Default skips them.
    bool   scoreAbstention = false;

    bool pipeline() const {
        return !fusion.empty() || enrich || neighbours > 0 || chunkWords > 0 ||
               userView || temporal > 0 || groupBeta > 0 || mmrLambda < 1.0 ||
               rerankTop > 0 || prfBeta > 0 || coverLambda > 0;
    }
};

std::vector<BenchQResult> runBenchCorpus(const BenchCorpus& corpus,
                                         const std::vector<int>& ks,
                                         const BenchRunOptions& opt = {});

// "pipeline dense fusion=rrf enrich chunk=90 ..." -- report mode label.
std::string describePipeline(const BenchRunOptions& opt);

struct BenchFileReport {
    std::string dataset;      // longmemeval | locomo
    std::string mode = "bm25";  // bm25 | semantic (alpha=..) | semantic-unavailable
    int corpora = 0;          // instances / samples processed
    int docs = 0;             // memory nodes stored in total
    int embedded = 0;         // docs that got a vector (semantic mode)
    double seconds = 0.0;
    std::vector<BenchQResult> results;
};

// Load a dataset file and run every corpus. maxCorpora <= 0 = all.
// Throws std::runtime_error on unreadable file / unknown dataset name.
BenchFileReport runBenchDatasetFile(const std::string& dataset,
                                    const std::string& path,
                                    BenchGranularity g,
                                    const std::vector<int>& ks,
                                    int maxCorpora = 0,
                                    const BenchRunOptions& opt = {});

// Human table (per-category + overall) or JSON (summary + per-question when
// perQuestion). Pure formatting.
std::string formatBenchReport(const BenchFileReport& rep, const std::vector<int>& ks,
                              bool json, bool perQuestion = false);

// "1,5,10" -> {1,5,10}; invalid / empty -> {1,5,10}.
std::vector<int> parseBenchKs(const std::string& csv);

// One stderr progress line: "[bench] corpus 50/500 (10%) 120s elapsed ~1080s left".
// ETA omitted until at least one corpus is done. Pure formatting.
std::string formatBenchProgress(int done, int total, double elapsedSec);

// Seconds between progress lines while bench-recall --dataset runs.
constexpr double kBenchProgressEverySec = 30.0;

}  // namespace icmg::imem
