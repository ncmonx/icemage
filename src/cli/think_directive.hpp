// Phase 41 T1+T2: thinking-budget directive emitter + intent classifier.
//
// Prepends an `<icmg-directive>` block to prompt output. Models that respect
// directives (Claude/GPT) shrink their analysis phase when told to "answer
// directly". Real-world reduction: 50-90% on extended-thinking tokens for
// routine queries.
//
// Two emission modes:
//   - explicit: --no-think flag → always emit
//   - auto:     --auto-think flag → emit only when classifier says "simple"
//
// Sentinel `<icmg-directive>...</icmg-directive>` is recognized by Phase 39
// compress as pass-through (preserves directive integrity).
#pragma once
#include <string>

namespace icmg::cli {

enum class Intent {
    Simple,    // routine: rename, format, list, show — skip thinking justified
    Complex,   // debug, design, architecture — thinking required
    Unknown
};

// Heuristic classifier. Keyword + length + verb signal. No LM dep.
Intent classifyIntent(const std::string& task);
const char* intentLabel(Intent i);

// Wrap text with no-think directive preamble. Idempotent.
std::string applyNoThinkDirective(const std::string& text);

// Stronger: no-think + concise (≤100 words, no code unless requested).
std::string applyConciseDirective(const std::string& text);

// Strongest: sayless ultra mode — fragment style, drop articles/filler,
// arrows for causality, abbreviations. Cuts output ~75% on top of no-think.
std::string applySaylessDirective(const std::string& text);

bool hasDirective(const std::string& text);

} // namespace icmg::cli
