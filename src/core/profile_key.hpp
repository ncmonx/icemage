#pragma once
// Pure normalization for the zoned profile/skill store. Canonicalizes zone + key to a
// safe slug ([a-z0-9_-]); validates the kind tag. No I/O.
#include <cctype>
#include <string>

namespace icmg::core {

inline std::string slugify(const std::string& in) {
    std::string out;
    bool lastDash = false;
    for (char ch : in) {
        unsigned char c = (unsigned char)ch;
        if (std::isalnum(c)) { out += (char)std::tolower(c); lastDash = false; }
        else if (!out.empty() && !lastDash) { out += '-'; lastDash = true; }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

inline std::string normalizeZone(const std::string& zone) {
    // Preserve a single leading underscore: it marks an INTERNAL/system zone
    // (_mode, _passphrase, _style, ...). slugify drops leading non-alnum, which
    // silently collapsed "_mode" -> "mode" and broke the internal-zone convention
    // (qa-frequent / qa-suggest could not tell machinery from real prompts).
    bool internal = !zone.empty() && zone[0] == '_';
    std::string s = slugify(zone);
    if (internal && (s.empty() || s[0] != '_')) s = "_" + s;
    return s.empty() ? std::string("default") : s;
}

inline std::string normalizeKey(const std::string& key) {
    return slugify(key);   // empty -> "" so caller can reject
}

inline std::string validKind(const std::string& kind) {
    if (kind == "skill" || kind == "note" || kind == "profile") return kind;
    return "profile";
}

}  // namespace icmg::core
