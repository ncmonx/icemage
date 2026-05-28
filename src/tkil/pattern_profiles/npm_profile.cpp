// v1.56 T1 Stage 3: npm install / yarn / pnpm pattern profile.
//
// Collapses high-frequency npm noise:
//   - "npm http fetch GET 200 https://registry.npmjs.org/<pkg>" bursts
//     during install (50-500 lines on a fresh install)
//   - "npm WARN deprecated ..." passes through (real signal)
//   - "added N packages" summary line passes through (real outcome)
// Error lines are protected by isAlwaysVerbatim in the upstream dedup pass.

#include "../pattern_pass.hpp"
#include "../dedup_pass.hpp"

#include <regex>
#include <sstream>
#include <string>

namespace icmg::tkil {

namespace {

bool matches(const std::string& cmd) {
    static const std::regex re(R"((^|\s)(npm|yarn|pnpm)\s+(install|i|add|ci)\b)");
    return std::regex_search(cmd, re);
}

std::string apply(const std::string& in) {
    static const std::regex fetch_re(R"(^npm http fetch .*$)");

    std::istringstream is(in);
    std::ostringstream os;
    std::string line;
    int fetch_run = 0;

    auto flushFetch = [&]() {
        if (fetch_run >= 3) {
            os << "npm http fetch ... (\xc3\x97" << fetch_run << " package fetches)\n";
        } else {
            // <3 occurrences: emit nothing extra (caller should not have
            // accumulated). We only collapse when worth collapsing.
        }
        fetch_run = 0;
    };

    while (std::getline(is, line)) {
        if (isAlwaysVerbatim(line)) {
            if (fetch_run > 0) flushFetch();
            os << line << '\n';
            continue;
        }
        if (std::regex_match(line, fetch_re)) {
            ++fetch_run;
            continue;
        }
        if (fetch_run > 0) {
            if (fetch_run < 3) {
                // Below threshold — replay nothing (the lines were silently
                // queued). Simpler & cleaner: when under threshold, we
                // didn't pass them through — that's by design for this
                // profile (npm http fetch is pure noise even at low count).
            } else {
                flushFetch();
            }
            fetch_run = 0;
        }
        os << line << '\n';
    }
    if (fetch_run > 0) flushFetch();

    return os.str();
}

}  // namespace

ICMG_REGISTER_PATTERN_PROFILE(npm, "npm-install", matches, apply)

}  // namespace icmg::tkil
