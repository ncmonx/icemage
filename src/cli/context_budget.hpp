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
#include <utility>
#include <cstdlib>
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

// --- Model context-window registry --------------------------------------
// The budget meter needs the REAL context window for the running model. A
// hardcoded 1M lies on 128K/200K models (warns far too late -> blows past
// compaction). This maps a model id (substring) to its documented window.
//
// Default = 200000 (the standard window for most modern models incl. the
// Claude API, OpenAI o-series). Choosing the SMALLER window as the fallback
// is the safe failure: an unknown model warns EARLY, never late. Only models
// with a genuinely larger/smaller documented window deviate from the default.
inline long long modelContextWindow(const std::string& model) {
    struct Entry { const char* needle; long long window; };
    // Priority order: first substring match wins. Most-specific first.
    static const Entry kTable[] = {
        // Claude Code effective 1M-context families + Gemini long-context
        {"opus-4",   1000000},
        {"sonnet-4", 1000000},
        {"gemini",   1000000},
        // 128K-window families
        {"gpt-4o",   128000},
        {"gpt-4",    128000},   // gpt-4-turbo etc. (modern default)
        {"deepseek", 128000},
        {"mistral",  128000},
        // Everything else (o-series, Haiku, Claude 3, unknown) -> default 200K.
    };
    for (const auto& e : kTable)
        if (model.find(e.needle) != std::string::npos) return e.window;
    return 200000;  // safe default: under-estimate -> nudge early, not late
}

// Tail-scan a transcript .jsonl (last ~256 KB) and return the LAST real
// "model":"..." value, skipping CC's "<synthetic>" turns. "" if none/unreadable.
inline std::string lastModelFromTranscript(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    std::streamoff start = sz > (256 * 1024) ? sz - (256 * 1024) : 0;
    f.seekg(start);
    std::string chunk((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::string key = "\"model\":\"";
    std::string last;
    size_t p = 0;
    while ((p = chunk.find(key, p)) != std::string::npos) {
        p += key.size();
        size_t end = chunk.find('"', p);
        if (end == std::string::npos) break;
        std::string m = chunk.substr(p, end - p);
        if (!m.empty() && m != "<synthetic>") last = m;  // keep last real
        p = end + 1;
    }
    return last;
}

// Resolve the context limit for a transcript: explicit env override wins, then
// the running model's documented window, then the safe default. Centralizes the
// limit logic so every call site (hook, `contextbudget --brief`) stays honest.
inline long long resolveContextLimit(const std::string& transcriptPath) {
    if (const char* e = std::getenv("ICMG_CONTEXT_LIMIT")) {
        try { long long v = std::stoll(e); if (v > 0) return v; } catch (...) {}
    }
    return modelContextWindow(lastModelFromTranscript(transcriptPath));
}

// --- Thinking-token accounting -------------------------------------------
// Thinking tokens are NOT broken out in the API usage object (they fold into
// output_tokens), so we ESTIMATE (~4 chars/token) from the transcript's
// "thinking" content blocks. Honest approximation — makes the otherwise
// invisible thinking cost visible per session. Not exact billing.
struct ThinkingSpend { long long est_tokens = 0; int blocks = 0; long long max_block = 0; };

inline ThinkingSpend sumThinkingTokens(const std::string& body) {
    ThinkingSpend r;
    const std::string marker = "\"type\":\"thinking\"";
    const std::string field  = "\"thinking\":\"";
    size_t p = 0;
    while ((p = body.find(marker, p)) != std::string::npos) {
        size_t tp = body.find(field, p);
        if (tp == std::string::npos) { p += marker.size(); continue; }
        tp += field.size();
        long long chars = 0;
        size_t i = tp;
        while (i < body.size()) {              // count to unescaped closing quote
            char c = body[i];
            if (c == '\\' && i + 1 < body.size()) { ++chars; i += 2; continue; }
            if (c == '"') break;
            ++chars; ++i;
        }
        long long t = chars / 4;               // ~4 chars/token
        r.est_tokens += t; ++r.blocks;
        if (t > r.max_block) r.max_block = t;
        p = (i > p) ? i : p + marker.size();
    }
    return r;
}

} // namespace icmg::cli
