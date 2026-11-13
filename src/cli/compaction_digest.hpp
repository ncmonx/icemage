// 2026-11-12: compaction fidelity helpers for `icmg compact-bg` (research item
// 7, docs/plans/2026-11-11-token-memory-research.md; after CliffCompaction,
// arXiv 2609.26779): a faithful compaction only truncates / drops, never
// rephrases, and never compacts a compaction -- it rebuilds from originals.
// Pure, no I/O.
#pragma once
#include "../imem/memory_node.hpp"
#include <string>
#include <vector>

namespace icmg::cli {

constexpr int    kCompactDigestLines    = 8;    // lines in a regex digest
constexpr size_t kCompactSentenceBytes  = 140;  // per-line sentence budget
constexpr size_t kCompactLlmBodyBytes   = 280;  // per-entry LLM prompt budget
constexpr int    kCompactRecallOverfetch = 3;   // recall limit x N, then filter

// Byte index one past the first sentence: a '.', '!' or '?' followed by
// whitespace or end of text (so "v2.28.0" / "1.5x" stay whole), or the
// first '\n' (excluded). npos when the text has no sentence end.
size_t sentenceEnd(const std::string& s);

// Recall hits minus compaction products (auto-compact-*, session-snapshot*,
// session:* -- see isSessionTopic), recall order kept, capped at `limit`.
std::vector<imem::MemoryNode> compactionInputs(const std::vector<imem::MemoryNode>& hits,
                                               size_t limit);

// One "- <topic>: <first sentence verbatim> [#id]" line per hit, at most
// maxLines; a sentence over maxSentenceBytes is cut on a UTF-8 boundary + "...".
std::string extractiveDigest(const std::vector<imem::MemoryNode>& hits, int maxLines,
                             size_t maxSentenceBytes);

// "src: #a #b ...\n" so every summary line can be traced to its originals;
// "" for no hits.
std::string sourceIdsLine(const std::vector<imem::MemoryNode>& hits);

}  // namespace icmg::cli
