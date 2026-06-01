#pragma once
// v1.79.0 ICM dual-memory: heuristic atomizer.
// Splits text into atomic propositions WITHOUT an LLM.
// Rules (in order):
//   1. Fenced code blocks (```...```) are emitted as a single atom, never split.
//   2. Newlines are line boundaries; leading "- "/"* " bullet markers stripped.
//   3. Remaining prose splits on . ! ? followed by space/EOL.
//   4. Trim; drop atoms shorter than 2 chars.
#include <string>
#include <vector>

namespace icmg::imem {

inline std::vector<std::string> atomSplit(const std::string& text) {
    std::vector<std::string> out;
    auto push = [&](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return;
        size_t b = s.find_last_not_of(" \t\r\n");
        s = s.substr(a, b - a + 1);
        if (s.size() >= 2 && (s[0] == '-' || s[0] == '*') && s[1] == ' ')
            s = s.substr(2);
        if (s.size() >= 2) out.push_back(std::move(s));
    };

    size_t i = 0, n = text.size();
    std::string cur;
    while (i < n) {
        if (text.compare(i, 3, "```") == 0) {
            push(cur); cur.clear();
            size_t end = text.find("```", i + 3);
            size_t close = (end == std::string::npos) ? n : end + 3;
            out.push_back(text.substr(i, close - i));   // fence verbatim
            i = close;
            continue;
        }
        char c = text[i];
        if (c == '\n') { push(cur); cur.clear(); ++i; continue; }
        cur.push_back(c);
        if ((c == '.' || c == '!' || c == '?') &&
            (i + 1 >= n || text[i + 1] == ' ' || text[i + 1] == '\n')) {
            push(cur); cur.clear();
        }
        ++i;
    }
    push(cur);
    return out;
}

} // namespace icmg::imem
