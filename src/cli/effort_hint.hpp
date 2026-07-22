// v2.20 (research #6): thinking-budget effort hint.
//
// Frontier models expose an extended-thinking budget (Anthropic budget_tokens);
// reasoning tokens are now the dominant cost variable, yet a token tool that
// only shrinks INPUT ignores them. This primitive emits a DETERMINISTIC effort
// recommendation from the existing intent classifier + the graph fan-out of the
// task (how many files/symbols the work touches) so a host can size its
// thinking budget: retrieval-simple -> low, cross-module refactor -> high.
//
// Advisory only, no LLM. Whether a host honors it is opt-in; the claim that the
// hint improves outcomes is a hypothesis -> gate behind a flag, measure first.
#pragma once
#include "think_directive.hpp"
#include <string>

namespace icmg::cli {

enum class EffortLevel { Low, Medium, High };

struct EffortHint {
    EffortLevel level = EffortLevel::Medium;
    int         budget_tokens = 8000;   // suggested extended-thinking budget
    std::string rationale;              // short human-facing reason
};

const char* effortLabel(EffortLevel lvl);

// Deterministic recommendation. fanOut = number of files/symbols the task
// touches (0 if unknown). Rules:
//   base by intent: Simple->Low(2000), Unknown->Medium(8000), Complex->High(16000)
//   fan-out escalation: >=8 bumps up one level; >=25 forces High.
EffortHint recommendEffort(Intent intent, int fanOut);

// Prepend an <icmg-effort budget=N level=L> directive block. Idempotent.
std::string applyEffortDirective(const std::string& text, const EffortHint& h);

// Count file/symbol markers in an assembled pack blob as a fan-out proxy
// (lines beginning "### " = symbols, "- " under a Files section, etc.).
// Deterministic; used when the caller has no explicit fan-out.
int estimateFanOut(const std::string& packBlob);

} // namespace icmg::cli
