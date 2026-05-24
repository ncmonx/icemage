// v1.37.0 C2: intent cache with regex-immediate hot path.
//
// Hot-path SLA <50ms p95 GUARANTEED:
//   - Cache lookup: PRIMARY KEY SELECT, <0.5ms typical
//   - Cache miss: regex classifier, <1ms
//   - LLM never invoked from hot path. Backfill is async (service tick).
//
// CI-lint enforce: this header must NOT pull in llama_runner.hpp.
#pragma once
#include <cstdint>
#include <string>

namespace icmg::core {

enum class Intent {
    Trivial,   // short prompt, simple question
    Code,      // implement/build/refactor
    Decision,  // architecture/design/trade-off
    Debug,     // error/bug/fix
    Default    // none matched
};

const char* intentName(Intent i);

class IntentCache {
public:
    // Hot path: lookup → regex fallback. Always returns within <2ms.
    // Side effect: on cache miss, INSERT regex result + queue for LLM backfill.
    static Intent classify(const std::string& prompt);

    // Manual cache query (no insert on miss). Returns Default on miss.
    static Intent lookup(const std::string& prompt);

    // Admin: count cache rows + queue depth.
    static int cacheSize();
    static int queueDepth();

    // Manual: pure regex classify (no DB touch).
    static Intent classifyRegex(const std::string& prompt);

    // Manual: compute prompt hash (FNV-64 hex).
    static std::string hashPrompt(const std::string& prompt);

    // Admin: clear all cache rows.
    static int clearAll();
};

} // namespace icmg::core
