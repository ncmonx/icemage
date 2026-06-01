// v1.37.0 C2: intent cache impl. See intent_cache.hpp.
#include "intent_cache.hpp"
#include "global_db.hpp"
#include "db.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <regex>
#include <sstream>

namespace icmg::core {

const char* intentName(Intent i) {
    switch (i) {
        case Intent::Trivial:  return "trivial";
        case Intent::Code:     return "code";
        case Intent::Decision: return "decision";
        case Intent::Debug:    return "debug";
        case Intent::Default:  return "default";
    }
    return "default";
}

namespace {

Intent intentFromName(const std::string& s) {
    if (s == "trivial")  return Intent::Trivial;
    if (s == "code")     return Intent::Code;
    if (s == "decision") return Intent::Decision;
    if (s == "debug")    return Intent::Debug;
    return Intent::Default;
}

// FNV-1a 64-bit; cheap + deterministic.
std::uint64_t fnv64(const std::string& s) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    return h;
}

std::string lc(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

} // namespace

std::string IntentCache::hashPrompt(const std::string& prompt) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(fnv64(prompt)));
    return std::string(buf);
}

Intent IntentCache::classifyRegex(const std::string& prompt) {
    if (prompt.empty()) return Intent::Default;
    std::string low = lc(prompt);

    // Trivial: very short OR yes/ok/skip/stop tokens
    if (prompt.size() < 30) {
        static const std::regex trivial_re(R"(^\s*(ya|ok|no|tidak|skip|stop|y|n|continue|lanjut)\s*[?!.]?\s*$)");
        if (std::regex_match(low, trivial_re)) return Intent::Trivial;
        if (prompt.size() < 8 && prompt.find('?') == std::string::npos) return Intent::Trivial;
    }

    // Debug
    static const std::regex debug_re(R"(\b(bug|error|fail|broken|crash|traceback|fix|exception|segfault|unauthorized|denied)\b)");
    if (std::regex_search(low, debug_re)) return Intent::Debug;

    // Code
    static const std::regex code_re(R"(\b(implement|add|create|build|refactor|new\s+(feature|cmd|file|module)|wrapper)\b)");
    if (std::regex_search(low, code_re)) return Intent::Code;

    // Decision
    static const std::regex dec_re(R"(\b(should|best|design|architect|trade.?off|choose|prefer|recommend)\b)");
    if (std::regex_search(low, dec_re)) return Intent::Decision;

    return Intent::Default;
}

Intent IntentCache::lookup(const std::string& prompt) {
    try {
        GlobalDb& gdb = GlobalDb::instance();
        std::string h = hashPrompt(prompt);
        Intent out = Intent::Default;
        bool hit = false;
        gdb.db().query(
            "SELECT intent FROM intent_cache WHERE prompt_hash = ?",
            { h }, [&](const Row& r){
                if (!r.empty()) { out = intentFromName(r[0]); hit = true; }
            });
        (void)hit;
        return out;
    } catch (...) { return Intent::Default; }
}

Intent IntentCache::classify(const std::string& prompt) {
    if (prompt.empty()) return Intent::Default;
    try {
        GlobalDb& gdb = GlobalDb::instance();
        std::string h = hashPrompt(prompt);
        Intent cached = Intent::Default;
        bool hit = false;
        gdb.db().query(
            "SELECT intent FROM intent_cache WHERE prompt_hash = ?",
            { h }, [&](const Row& r){
                if (!r.empty()) { cached = intentFromName(r[0]); hit = true; }
            });
        if (hit) return cached;

        // Miss → regex classify + insert + queue for backfill.
        Intent rx = classifyRegex(prompt);
        std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
        try {
            gdb.db().run(
                "INSERT OR IGNORE INTO intent_cache(prompt_hash, intent, source, created_at, updated_at) "
                "VALUES(?, ?, 'regex', ?, ?)",
                { h, intentName(rx), std::to_string(now), std::to_string(now) });
            gdb.db().run(
                "INSERT OR IGNORE INTO intent_backfill_queue(prompt_hash, prompt_text, queued_at) "
                "VALUES(?, ?, ?)",
                { h, prompt.substr(0, 4000), std::to_string(now) });
        } catch (...) {}
        return rx;
    } catch (...) {
        // Fail-soft: never block hot path. Pure regex if DB unavailable.
        return classifyRegex(prompt);
    }
}

int IntentCache::cacheSize() {
    try {
        GlobalDb& gdb = GlobalDb::instance();
        int n = 0;
        gdb.db().query("SELECT COUNT(*) FROM intent_cache", {}, [&](const Row& r){
            if (!r.empty()) { try { n = std::stoi(r[0]); } catch (...) {} }
        });
        return n;
    } catch (...) { return 0; }
}

int IntentCache::queueDepth() {
    try {
        GlobalDb& gdb = GlobalDb::instance();
        int n = 0;
        gdb.db().query("SELECT COUNT(*) FROM intent_backfill_queue", {}, [&](const Row& r){
            if (!r.empty()) { try { n = std::stoi(r[0]); } catch (...) {} }
        });
        return n;
    } catch (...) { return 0; }
}

int IntentCache::clearAll() {
    try {
        GlobalDb& gdb = GlobalDb::instance();
        gdb.db().run("DELETE FROM intent_cache",          {});
        gdb.db().run("DELETE FROM intent_backfill_queue", {});
        return 0;
    } catch (...) { return 1; }
}

} // namespace icmg::core
