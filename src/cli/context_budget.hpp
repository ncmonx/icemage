#pragma once
// Context-budget gauge: turn a CC transcript's recorded API usage into a
// "% of context window used" reading -- gives the (otherwise context-blind)
// model a real meter to pace work + checkpoint before compaction.
//
// The transcript .jsonl records each assistant turn's usage. The live context
// size = input_tokens + cache_creation_input_tokens + cache_read_input_tokens
// of the LAST usage line (these are the real API numbers, not an estimate).
#include <string>

namespace icmg::cli {

struct BudgetInfo { long long used = 0, limit = 0; int pctUsed = 0, pctLeft = 100; };

inline BudgetInfo computeBudget(long long used, long long limit) {
    BudgetInfo b; b.used = used; b.limit = limit;
    if (limit <= 0) { b.pctUsed = 0; b.pctLeft = 100; return b; }  // no limit -> safe
    long long p = (used * 100) / limit;
    if (p < 0) p = 0; if (p > 100) p = 100;
    b.pctUsed = (int)p; b.pctLeft = 100 - (int)p;
    return b;
}

inline std::string formatBudget(const BudgetInfo& b) {
    return "[context: ~" + std::to_string(b.pctUsed) + "% used, ~"
         + std::to_string(b.pctLeft) + "% left | "
         + std::to_string(b.used) + "/" + std::to_string(b.limit) + " tok]";
}

// Parse the integer after "<key>": in a JSON line (0 if absent). Exact-key:
// "input_tokens": does NOT match "cache_read_input_tokens": (char before is not ").
inline long long extractLL(const std::string& s, const std::string& key) {
    std::string pat = "\"" + key + "\":";
    size_t p = s.find(pat);
    if (p == std::string::npos) return 0;
    p += pat.size();
    while (p < s.size() && s[p] == ' ') ++p;
    bool neg = false, any = false; long long v = 0;
    if (p < s.size() && s[p] == '-') { neg = true; ++p; }
    while (p < s.size() && s[p] >= '0' && s[p] <= '9') { v = v * 10 + (s[p] - '0'); ++p; any = true; }
    return any ? (neg ? -v : v) : 0;
}

// Sum the three input components = live context size for a usage line.
inline long long contextTokensFromUsageLine(const std::string& line) {
    return extractLL(line, "input_tokens")
         + extractLL(line, "cache_creation_input_tokens")
         + extractLL(line, "cache_read_input_tokens");
}

} // namespace icmg::cli
