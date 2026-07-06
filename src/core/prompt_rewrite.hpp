#pragma once
// Pre-flight prompt rewrite (feature #8), layered on the TE2 salience compressor
// (feature #6) in compress_select.hpp.
//
// `icmg agent --rewrite` shrinks the bulky PACKED-CONTEXT region of an assembled
// prompt before it is sent to the (possibly metered) downstream LLM, while the
// system prompt and the task instruction are protected verbatim. Two guardrails
// straight from the research plan:
//   1. Protect templated/instruction prompts -> rewriteAssembled only ever
//      touches the middle `body`, never `head`/`tail`.
//   2. Measure total cost + honesty gate -> if compression does not actually
//      shrink the context, the ORIGINAL is kept (never blow the prompt up).
//
// Pure + deterministic (model-free salience), so the plan's "English code gains
// little from an LLM rewriter" caveat does not apply -- this is not an LLM pass.

#include "compress_select.hpp"
#include <string>
#include <vector>
#include <cstddef>

namespace icmg::core {

// ~token estimate (chars/4), matching icmg's convention elsewhere.
inline int estimateTokens(const std::string& s) {
    return (int)(s.size() / 4);
}

struct RewriteReport {
    std::size_t before_chars  = 0;
    std::size_t after_chars   = 0;
    int         before_tokens = 0;
    int         after_tokens  = 0;
    bool        applied       = false;   // false => original kept (gate failed / not needed)
};

// Split text into newline-delimited spans (keeps content, drops the '\n').
inline std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Compress `context` to fit budgetChars using TE2 salience selection. Honesty
// gate: if the context is already within budget, or compression fails to
// shrink it, return the original unchanged with applied=false.
inline std::string compressContext(const std::string& context,
                                   std::size_t budgetChars,
                                   RewriteReport& rep) {
    rep.before_chars  = context.size();
    rep.before_tokens = estimateTokens(context);
    rep.after_chars   = context.size();
    rep.after_tokens  = rep.before_tokens;
    rep.applied       = false;

    if (context.size() <= budgetChars) return context;  // already fits

    auto lines = splitLines(context);
    if (lines.empty()) return context;

    // Rewrite-specific policy (does NOT alter TE2): drop pure-boilerplate lines
    // (infoScore == 0: dashes, blanks, separators) so leftover budget isn't
    // spent re-including no-signal filler. Guard: if that removes everything,
    // fall back to the full set so we never nuke all content.
    std::vector<std::string> kept;
    std::vector<double> keptScores;
    for (const auto& ln : lines) {
        double sc = infoScore(ln);
        if (sc > 0.0) { kept.push_back(ln); keptScores.push_back(sc); }
    }
    if (kept.empty()) {
        kept = lines;
        keptScores.clear();
        keptScores.reserve(lines.size());
        for (const auto& ln : lines) keptScores.push_back(infoScore(ln));
    }

    std::string compressed = selectByBudget(kept, keptScores, budgetChars, "\n");

    // Honesty gate: only accept a strictly smaller result.
    if (compressed.size() >= context.size() || compressed.empty())
        return context;

    rep.after_chars   = compressed.size();
    rep.after_tokens  = estimateTokens(compressed);
    rep.applied       = true;
    return compressed;
}

// Reassemble a prompt as head + compressed(body) + tail. head and tail (system
// prompt / task instruction) are emitted VERBATIM; only body is compressed.
inline std::string rewriteAssembled(const std::string& head,
                                    const std::string& body,
                                    const std::string& tail,
                                    std::size_t budgetChars,
                                    RewriteReport& rep) {
    std::string newBody = compressContext(body, budgetChars, rep);
    return head + newBody + tail;
}

}  // namespace icmg::core
