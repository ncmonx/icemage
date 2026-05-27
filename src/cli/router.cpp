#include "router.hpp"
#include <algorithm>
#include <cctype>

namespace icmg::cli {

namespace {

bool containsAnyToken(const std::string& lower, const char* const* tokens, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (lower.find(tokens[i]) != std::string::npos) return true;
    }
    return false;
}

std::string toLower(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (char c : s) out += (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
    return out;
}

}  // anon

RouteDecision classifyPrompt(const std::string& text) {
    RouteDecision d;
    if (text.empty()) {
        d.route = Route::CLOUD;
        d.confidence = 0.1;
        d.reason = "empty";
        return d;
    }
    if (text.size() > 400) {
        d.route = Route::CLOUD;
        d.confidence = 0.6;
        d.reason = "long (>400 chars)";
        return d;
    }

    auto lc = toLower(text);

    // Tool verbs -> CLOUD (override short-length check).
    static const char* tool_verbs[] = {
        "refactor", "debug", "build", "deploy", "test", "compile",
        "grep ", "git ", "edit ", "patch", "fix ", "implement",
        "create file", "write file", "modify file", "delete file",
    };
    if (containsAnyToken(lc, tool_verbs,
                          sizeof(tool_verbs)/sizeof(tool_verbs[0]))) {
        d.route = Route::CLOUD;
        d.confidence = 0.85;
        d.reason = "tool verb detected";
        return d;
    }

    // Short + question keywords -> LOCAL.
    static const char* question_kw[] = {
        "apa itu", "apa ", "jelaskan", "bagaimana", "tolong",
        "what is", "what ", "how ", "why ", "define ", "list ",
        "explain", "describe", "format ", "translate",
    };
    if (text.size() < 200 &&
        containsAnyToken(lc, question_kw,
                          sizeof(question_kw)/sizeof(question_kw[0]))) {
        d.route = Route::LOCAL;
        d.confidence = 0.8;
        d.reason = "short question";
        return d;
    }

    // Default -> CLOUD (low confidence).
    d.route = Route::CLOUD;
    d.confidence = 0.3;
    d.reason = "default fallback";
    return d;
}

}  // namespace icmg::cli
