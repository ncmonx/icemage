// v1.56 T1 Stage 3: `git push` pattern profile.
//
// Collapses 'remote:' banner spam (GitHub motd, security scan notices,
// help links) into a single summary line. Preserves:
//   - any 'remote: error:' / 'remote: rejected' line (real signal)
//   - the final "To <url>" + ref-update lines (the outcome)
//   - bare "error:" / "fatal:" lines from local git (via isAlwaysVerbatim)

#include "../pattern_pass.hpp"
#include "../dedup_pass.hpp"

#include <regex>
#include <sstream>
#include <string>

namespace icmg::tkil {

namespace {

bool matches(const std::string& cmd) {
    static const std::regex re(R"((^|\s)git\s+push\b)");
    return std::regex_search(cmd, re);
}

// Critical 'remote:' line that MUST pass through.
bool isCriticalRemote(const std::string& line) {
    static const std::regex re(
        R"(remote:\s*(error|fatal|rejected|GH\d{3}|warning|denied|forbidden))",
        std::regex::icase);
    return std::regex_search(line, re);
}

std::string apply(const std::string& in) {
    static const std::regex remote_re(R"(^remote:.*$)");
    static const std::regex outcome_re(R"(^(To\s+\S+|\s*[+!*]?\s*\[?[a-f0-9]+\.\.[a-f0-9]+\]?\s+\S+\s+->\s+\S+|\s*!\s+\[.*\]\s+\S+\s+->\s+\S+).*$)");

    std::istringstream is(in);
    std::ostringstream os;
    std::string line;
    int remote_run = 0;

    auto flushRemote = [&]() {
        if (remote_run >= 2) {
            os << "remote: ... (\xc3\x97" << remote_run << " banner lines collapsed)\n";
        }
        remote_run = 0;
    };

    while (std::getline(is, line)) {
        // Always-verbatim error/fatal/etc.
        if (isAlwaysVerbatim(line)) {
            flushRemote();
            os << line << '\n';
            continue;
        }
        // Outcome lines (To <url>, ref-updates) always pass.
        if (std::regex_match(line, outcome_re)) {
            flushRemote();
            os << line << '\n';
            continue;
        }
        // Critical remote: passes verbatim.
        if (std::regex_match(line, remote_re)) {
            if (isCriticalRemote(line)) {
                flushRemote();
                os << line << '\n';
            } else {
                ++remote_run;
            }
            continue;
        }
        // Anything else passes through.
        flushRemote();
        os << line << '\n';
    }
    flushRemote();
    return os.str();
}

}  // namespace

ICMG_REGISTER_PATTERN_PROFILE(git_push, "git-push", matches, apply)

}  // namespace icmg::tkil
