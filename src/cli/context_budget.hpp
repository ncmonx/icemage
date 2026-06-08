#pragma once
// Context-budget gauge: turn a CC transcript's recorded API usage into a
// "% of context window used" reading -- gives the (otherwise context-blind)
// model a real meter to pace work + checkpoint before compaction.
//
// The transcript .jsonl records each assistant turn's usage. The live context
// size = input_tokens + cache_creation_input_tokens + cache_read_input_tokens
// of the LAST usage line (these are the real API numbers, not an estimate).
#include <string>

#include <fstream>
#include <iterator>
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

// Tail-scan a transcript .jsonl (last ~512 KB) and return the LAST usage line's
// context-token sum. 0 if no usage / unreadable. Bounded so it stays cheap.
inline long long lastContextTokensFromTranscript(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0;
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    std::streamoff start = sz > (512 * 1024) ? sz - (512 * 1024) : 0;
    f.seekg(start);
    std::string chunk((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    long long used = 0;
    size_t pos = 0;
    while (pos <= chunk.size()) {
        size_t nl = chunk.find('\n', pos);
        std::string line = chunk.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (line.find("input_tokens") != std::string::npos) {
            long long t = contextTokensFromUsageLine(line);
            if (t > 0) used = t;   // keep last
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return used;
}

} // namespace icmg::cli
