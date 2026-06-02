#pragma once
// v2.0.0 search snapshot: turn arbitrary user input into a safe FTS5 MATCH string.
// Tokens (alnum + underscore, length >= 2) become prefix terms joined by space
// (implicit AND). All FTS5 operators/quotes are dropped, so user input can never
// inject MATCH syntax. Pure + header-only.
#include <cctype>
#include <string>

namespace icmg::core {

inline std::string ftsQuery(const std::string& input) {
    std::string out, cur;
    auto flush = [&]() {
        if (cur.size() >= 2) {
            if (!out.empty()) out += ' ';
            out += cur;
            out += '*';            // prefix match
        }
        cur.clear();
    };
    for (char ch : input) {
        unsigned char c = (unsigned char)ch;
        if (std::isalnum(c) || c == '_') cur.push_back((char)c);
        else flush();
    }
    flush();
    return out;
}

}  // namespace icmg::core
