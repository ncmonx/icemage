#include "think_directive.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

namespace {

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

static int wordCount(const std::string& s) {
    int n = 0; bool in_word = false;
    for (char c : s) {
        bool ws = std::isspace((unsigned char)c) || c == '\n';
        if (!ws && !in_word) { ++n; in_word = true; }
        else if (ws) in_word = false;
    }
    return n;
}

// Verb/keyword heuristics — conservative; uncovered → Unknown (defaults keep
// thinking ON, no harm).
static const std::vector<std::string> SIMPLE_KEYWORDS = {
    "rename", "format", "list", "show", "count", "print", "dump", "export",
    "view", "ls", "cat", "echo", "what is", "what's", "find ", "locate",
    "where is", "remove", "delete file", "copy", "move file", "lookup"
};

static const std::vector<std::string> COMPLEX_KEYWORDS = {
    "debug", "fix bug", "refactor", "design", "architecture", "race",
    "deadlock", "security", "audit", "review", "optimize", "performance",
    "concurrency", "thread-safe", "migrate", "rewrite", "implement",
    "why does", "explain", "analyze", "investigate", "trace", "diagnose"
};

bool containsAny(const std::string& s, const std::vector<std::string>& kws) {
    for (auto& k : kws) {
        if (s.find(k) != std::string::npos) return true;
    }
    return false;
}

} // namespace

Intent classifyIntent(const std::string& task) {
    if (task.empty()) return Intent::Unknown;
    std::string t = lower(task);

    // Complex wins on tie — thinking-on default is safer.
    if (containsAny(t, COMPLEX_KEYWORDS)) return Intent::Complex;
    if (containsAny(t, SIMPLE_KEYWORDS)) return Intent::Simple;

    int wc = wordCount(t);
    if (wc <= 5) return Intent::Simple;
    if (wc >= 12) return Intent::Complex;
    return Intent::Unknown;
}

const char* intentLabel(Intent i) {
    switch (i) {
        case Intent::Simple:  return "simple";
        case Intent::Complex: return "complex";
        case Intent::Unknown: return "unknown";
    }
    return "unknown";
}

bool hasDirective(const std::string& text) {
    return text.find("<icmg-directive>") != std::string::npos;
}

std::string applyNoThinkDirective(const std::string& text) {
    if (hasDirective(text)) return text;
    std::ostringstream os;
    os << "<icmg-directive>\n"
       << "Answer directly without analysis. No exploration phase. "
       << "Single-pass response. Skip your usual reasoning section.\n"
       << "</icmg-directive>\n"
       << text;
    return os.str();
}

std::string applyConciseDirective(const std::string& text) {
    if (hasDirective(text)) return text;
    std::ostringstream os;
    os << "<icmg-directive>\n"
       << "Answer directly without analysis. No exploration. Single-pass response. "
       << "Reply in under 100 words. No code blocks unless specifically requested. "
       << "No explanations beyond what is asked.\n"
       << "</icmg-directive>\n"
       << text;
    return os.str();
}

std::string applySaylessDirective(const std::string& text) {
    if (hasDirective(text)) return text;
    std::ostringstream os;
    os << "<icmg-directive>\n"
       << "Sayless mode ultra. Answer directly, no analysis, no exploration. "
       << "Drop articles (a/an/the), drop filler (just/really/basically/actually/simply), "
       << "drop pleasantries. Use fragments. Use arrows for causality (X → Y). "
       << "Abbreviate where unambiguous (DB, auth, config, fn, impl). "
       << "One word when one word enough. Pattern: [thing] [action] [reason]. "
       << "Code/commits/security text: keep normal English. "
       << "Reply under 60 words unless user explicitly requested more.\n"
       << "</icmg-directive>\n"
       << text;
    return os.str();
}

} // namespace icmg::cli
