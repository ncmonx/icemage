// 2026-11-12: rerank-gate research trace (bench-only).
//
// When ICMG_BENCH_GATE_TRACE=<path> is set, runBenchPipeline appends one JSON
// line per scored question with every signal a rerank gate could use (raw BM25
// top-1/top-2, fused top-1/top-2, query coverage of the BM25 and fused top-1),
// the ranked list before and after the cross-encoder, and the gold ids.
// Any gate policy can then be replayed offline (skipped -> pre, else post)
// without re-running the cross-encoder per threshold.
#pragma once
#include <string>
#include <vector>

namespace icmg::imem {

inline constexpr const char* kGateTraceEnv = "ICMG_BENCH_GATE_TRACE";

struct GateTrace {
    std::string qid, category;
    double bmS1 = 0, bmS2 = 0;        // raw BM25 of lexical rank 1 / 2
    double fusedS1 = 0, fusedS2 = 0;  // fused score of final pre-rerank rank 1 / 2
    double bmCov = 0, fusedCov = 0;   // queryCoverage of those two top-1 docs
    std::vector<std::string> pre, post, gold;
};

// Share of DISTINCT query tokens present in docTokens; empty query -> 0.
double queryCoverage(const std::vector<std::string>& docTokens,
                     const std::vector<std::string>& queryTokens);

// Single-line JSON (no trailing newline).
std::string gateTraceJson(const GateTrace& t);

// Path from kGateTraceEnv, or "" when tracing is off.
std::string gateTracePath();

// Thread-safe append of gateTraceJson(t) + '\n' to path.
void appendGateTrace(const std::string& path, const GateTrace& t);

}  // namespace icmg::imem
