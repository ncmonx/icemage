// v1.56 T1 Stage 3: `gh api` pattern profile.
//
// Collapses large single-line JSON object responses (>300 bytes) into a
// "{N keys}" summary so AI sees the shape without burning tokens on every
// field. Short JSON / non-JSON output passes through unchanged. Arrays
// (`[...]`) are NOT collapsed here — they often contain useful index info;
// caller should pipe to `jq` if they want a different shape.

#include "../pattern_pass.hpp"
#include "../dedup_pass.hpp"

#include <regex>
#include <sstream>
#include <string>

namespace icmg::tkil {

namespace {

bool matches(const std::string& cmd) {
    static const std::regex re(R"((^|\s)gh\s+api\b)");
    return std::regex_search(cmd, re);
}

// Count top-level "key": occurrences inside a JSON line. Cheap heuristic
// (does not parse nested objects properly, just counts top-level key colons).
int countTopLevelKeys(const std::string& line) {
    int depth = 0;
    int keys  = 0;
    bool in_string = false;
    bool escape = false;
    bool key_open = false;  // we're between `"` and `":` at depth 1
    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (escape) { escape = false; continue; }
        if (c == '\\') { escape = true; continue; }
        if (in_string) {
            if (c == '"') {
                in_string = false;
                if (depth == 1 && key_open) {
                    // look ahead for ':' (possibly after whitespace)
                    std::size_t j = i + 1;
                    while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) ++j;
                    if (j < line.size() && line[j] == ':') ++keys;
                }
                key_open = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            if (depth == 1) key_open = true;
            continue;
        }
        if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') --depth;
    }
    return keys;
}

std::string apply(const std::string& in) {
    constexpr std::size_t kMinJsonSize = 300;

    std::istringstream is(in);
    std::ostringstream os;
    std::string line;
    while (std::getline(is, line)) {
        if (isAlwaysVerbatim(line)) {
            os << line << '\n';
            continue;
        }
        // Single-line JSON object detection: starts with '{', ends with '}',
        // and exceeds the minimum-size threshold.
        if (line.size() >= kMinJsonSize
            && !line.empty() && line.front() == '{' && line.back() == '}') {
            int n = countTopLevelKeys(line);
            os << "{... " << n << " keys, " << line.size() << " bytes ...}\n";
            continue;
        }
        os << line << '\n';
    }
    return os.str();
}

}  // namespace

ICMG_REGISTER_PATTERN_PROFILE(gh_api, "gh-api", matches, apply)

}  // namespace icmg::tkil
