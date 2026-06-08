// libFuzzer target: the context-budget transcript parsers consume UNTRUSTED
// input (a Claude Code transcript .jsonl line), so they are a high-value fuzz
// surface. All three entrypoints are pure (no IO, no alloc beyond std::string),
// header-only -- the harness compiles standalone against src/cli/context_budget.hpp.
#include <cstddef>
#include <cstdint>
#include <string>
#include "cli/context_budget.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string line(reinterpret_cast<const char*>(data), size);
    // Exact-key int scan over arbitrary bytes.
    (void)icmg::cli::extractLL(line, "input_tokens");
    (void)icmg::cli::extractLL(line, "cache_read_input_tokens");
    // Sum of the three input components for a (possibly malformed) usage line.
    long long used = icmg::cli::contextTokensFromUsageLine(line);
    // Percent math must never divide-by-zero / overflow on hostile values.
    (void)icmg::cli::computeBudget(used, 1000000);
    (void)icmg::cli::computeBudget(used, 0);
    return 0;
}
