#pragma once
// <private> redaction for `icmg store`.
// Spec backlog: docs/2026-06-15-recall-progressive-disclosure.md sec 11.
// Riset asal: claude-mem "<private> tags to exclude sensitive content".
//
// Semantics: any region wrapped in <private>...</private> is stripped from the
// content BEFORE it is persisted, so the sensitive bytes never reach the DB.
// Fail-safe: an unterminated <private> (no closing tag) redacts to end-of-string
// -- we would rather drop trailing text than leak something marked private.
// Pure functions (no I/O) so they are unit-testable in isolation.

#include <string>

namespace icmg::cli {

inline bool hasPrivate(const std::string& s) {
    return s.find("<private>") != std::string::npos;
}

inline std::string stripPrivate(const std::string& s) {
    static const std::string OPEN  = "<private>";
    static const std::string CLOSE = "</private>";
    if (s.find(OPEN) == std::string::npos) return s;
    std::string out;
    out.reserve(s.size());
    size_t pos = 0;
    while (pos < s.size()) {
        size_t open = s.find(OPEN, pos);
        if (open == std::string::npos) {        // no more spans
            out.append(s, pos, std::string::npos);
            break;
        }
        out.append(s, pos, open - pos);          // keep text before the tag
        size_t close = s.find(CLOSE, open + OPEN.size());
        if (close == std::string::npos) {        // unterminated -> redact to end
            break;
        }
        pos = close + CLOSE.size();              // skip the whole span
    }
    // collapse a possible double space left where a span was removed mid-line.
    std::string tidy;
    tidy.reserve(out.size());
    bool prev_sp = false;
    for (char c : out) {
        bool sp = (c == ' ');
        if (sp && prev_sp) continue;
        tidy.push_back(c);
        prev_sp = sp;
    }
    return tidy;
}

} // namespace icmg::cli
