// 2026-11-11: budget-shaping ladder for `icmg context` file bodies (research
// item 4, docs/plans/2026-11-11-token-memory-research.md; after Rivet,
// arXiv 2610.34967: pick the richest representation that fits the budget).
// Over budget, instead of a blind byte cut that can stop mid-line, try:
//   full             body fits as is
//   prefix+skeleton  whole lines up to P, then the signature of every symbol
//                    that starts after P (nothing silently disappears)
//   skeleton         P = 0: signatures only
//   truncate         no symbols / even the skeleton is too big: whole lines
// Pure, no I/O.
#pragma once
#include <string>
#include <vector>

namespace icmg::cli {

// Bytes the caller keeps free for its own "--- Content ... ---" header and
// truncation notice, so the outer output cap never cuts a skeleton line.
constexpr size_t      kLadderHeadroom   = 256;
constexpr const char* kLadderSkelHeader = "--- skeleton (signatures past the cut) ---\n";

struct LadderSig {
    int         line = 0;   // 1-based start line
    std::string text;       // first line of the symbol (signature)
};

struct LadderResult {
    std::string text;
    std::string rung;          // full | prefix+skeleton | skeleton | truncate
    int         keptLines = 0; // whole body lines kept (all when full)
};

LadderResult fitBodyToBudget(const std::string& body, const std::vector<LadderSig>& sigs,
                             size_t budget);

}  // namespace icmg::cli
