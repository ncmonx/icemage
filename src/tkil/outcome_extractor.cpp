// v1.56 T1 Stage 4: outcome-only mode — implementation.
//
// Per-cmd outcome extractors keyed by cmdline regex. Each returns the
// reduced view as a string. Error / fatal lines (isAlwaysVerbatim) always
// pass through regardless of which extractor runs.

#include "outcome_extractor.hpp"
#include "dedup_pass.hpp"   // isAlwaysVerbatim

#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::tkil {

namespace {

// Cmdline-matchers.
const std::regex& reGhRelease() {
    static const std::regex r(R"((^|\s)gh\s+release\s+(create|upload|edit)\b)");
    return r;
}
const std::regex& reGitPush() {
    static const std::regex r(R"((^|\s)git\s+push\b)");
    return r;
}
const std::regex& reCurlDownload() {
    // -o or -O flag triggers (single output-file mode).
    static const std::regex r(R"((^|\s)curl\b.*(-o\s+\S+|--output\s+\S+|-O\b))");
    return r;
}
const std::regex& reCmakeBuild() {
    static const std::regex r(R"((^|\s)(cmake\s+--build|ninja|msbuild)\b)");
    return r;
}

// Line-pattern matchers (per outcome type).
const std::regex& reGhUrl() {
    static const std::regex r(R"(^https?://[^\s]+/releases/(tag|download)/.+$)");
    return r;
}
const std::regex& reGitPushTo() {
    static const std::regex r(R"(^To\s+\S+)");
    return r;
}
const std::regex& reGitPushRef() {
    // Captures common ref-update formats including "[deleted]", "[new branch]",
    // "[remote rejected]", and the standard "abc..def" range form.
    static const std::regex r(
        R"(^\s*[+!*=>]?\s*(\[?[a-f0-9]+\.\.[a-f0-9]+\]?|\[(new branch|new tag|deleted|remote rejected|rejected)\])\s+\S+\s+->\s+\S+.*$)");
    return r;
}
const std::regex& reCmakeLinking() {
    static const std::regex r(R"(^\[\d+/\d+\]\s+Linking\b.*$)");
    return r;
}
const std::regex& reCurlProgressFinal() {
    // The last "% Received" progress line — starts with "100 " (100% done).
    static const std::regex r(R"(^\s*100\s+\S+.*$)");
    return r;
}

// Helper: stream through lines, emit anything matching `keep_re` and
// anything that isAlwaysVerbatim (errors). Skip everything else.
std::string filterLines(const std::string& in, const std::regex& keep_re) {
    std::istringstream is(in);
    std::ostringstream os;
    std::string line;
    while (std::getline(is, line)) {
        if (isAlwaysVerbatim(line) || std::regex_search(line, keep_re)) {
            os << line << '\n';
        }
    }
    return os.str();
}

// Helper: like filterLines but for keep_re only the LAST match is emitted
// (intermediate matches are dropped). isAlwaysVerbatim lines are always
// emitted verbatim in order. Used by cmake-build (only the final Linking
// line is the outcome — earlier Linking lines are intermediate static-lib
// builds, not the final exe).
std::string filterLastOnly(const std::string& in, const std::regex& keep_re) {
    std::istringstream is(in);
    std::ostringstream os;
    std::string line;
    std::string last_match;
    bool have_match = false;
    while (std::getline(is, line)) {
        if (isAlwaysVerbatim(line)) {
            os << line << '\n';
            continue;
        }
        if (std::regex_search(line, keep_re)) {
            last_match = line;
            have_match = true;
        }
    }
    if (have_match) os << last_match << '\n';
    return os.str();
}

// Helper: same but with multiple "keep" regexes (OR).
std::string filterLinesAny(const std::string& in,
                            const std::vector<const std::regex*>& keeps) {
    std::istringstream is(in);
    std::ostringstream os;
    std::string line;
    while (std::getline(is, line)) {
        if (isAlwaysVerbatim(line)) {
            os << line << '\n';
            continue;
        }
        for (const std::regex* r : keeps) {
            if (std::regex_search(line, *r)) {
                os << line << '\n';
                break;
            }
        }
    }
    return os.str();
}

}  // namespace

bool outcomeEligible(const std::string& cmdline) {
    return std::regex_search(cmdline, reGhRelease())
        || std::regex_search(cmdline, reGitPush())
        || std::regex_search(cmdline, reCurlDownload())
        || std::regex_search(cmdline, reCmakeBuild());
}

std::string outcomeOnly(const std::string& input, const std::string& cmdline) {
    if (input.empty()) return input;

    if (std::regex_search(cmdline, reGhRelease())) {
        return filterLines(input, reGhUrl());
    }
    if (std::regex_search(cmdline, reGitPush())) {
        return filterLinesAny(input, {&reGitPushTo(), &reGitPushRef()});
    }
    if (std::regex_search(cmdline, reCmakeBuild())) {
        // Only the final Linking line is the outcome — intermediate static
        // libs are noise here.
        return filterLastOnly(input, reCmakeLinking());
    }
    if (std::regex_search(cmdline, reCurlDownload())) {
        // Only the final 100% progress line matters; earlier rows are noise.
        return filterLastOnly(input, reCurlProgressFinal());
    }
    return input;
}

}  // namespace icmg::tkil
