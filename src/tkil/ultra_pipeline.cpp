// v1.56 T1: Tkil Ultra pipeline orchestrator — implementation.

#include "ultra_pipeline.hpp"
#include "dedup_pass.hpp"
#include "pattern_pass.hpp"
#include "outcome_extractor.hpp"
#include "session_glossary.hpp"

#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>

namespace icmg::tkil {

namespace {

// Process-wide glossary so call N+1 in the same `icmg` invocation can reuse
// tokens minted in call N. Cross-invocation persistence is added later by
// the T3 long-lived daemon (which holds one map per session_id).
SessionGlossary& processGlossary() {
    static SessionGlossary g;
    return g;
}

std::mutex& glossaryMutex() {
    static std::mutex m;
    return m;
}

}  // namespace

double duplicationRatio(const std::string& text) {
    if (text.empty()) return 0.0;
    std::istringstream is(text);
    std::string line;
    int total = 0;
    std::unordered_set<std::string> distinct;
    while (std::getline(is, line)) {
        ++total;
        distinct.insert(line);
    }
    if (total == 0) return 0.0;
    return static_cast<double>(total - static_cast<int>(distinct.size()))
           / static_cast<double>(total);
}

bool autoTriggerUltra(const std::string& text) {
    return text.size() > 5 * 1024 && duplicationRatio(text) > 0.4;
}

std::string applyUltraPipeline(const std::string& input,
                                const std::string& cmdline) {
    if (input.empty()) return input;

    // Stage 0: microcompact — surgical truncation if output oversized.
    std::string s = microcompact(input);

    // Stage 2: dedup pass.
    s = dedupPass(s);

    // Stage 3: pattern collapse (no-op if no profile matches cmdline).
    s = patternPass(s, cmdline);

    // Stage 4: outcome-only (no-op for non-eligible cmdlines).
    if (outcomeEligible(cmdline)) {
        s = outcomeOnly(s, cmdline);
    }

    // Stage 5: session glossary (process-wide).
    {
        std::lock_guard<std::mutex> g(glossaryMutex());
        s = processGlossary().apply(s);
    }

    return s;
}

}  // namespace icmg::tkil
