// v1.56 T1 Stage 2: dedup pass — implementation.
//
// Streaming O(n) collapse of adjacent identical / near-identical lines.
// See dedup_pass.hpp for the public contract.

#include "dedup_pass.hpp"

#include <cstring>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::tkil {

namespace {

// Always-verbatim regex — error/fail/FATAL/FAILED + linker LNK\d{4} +
// resource RC\d{4} + MSVC compiler C\d{4}: prefixes.
// Case-insensitive on the word triggers; numeric codes are anchored exact.
static const std::regex& verbatimRe() {
    static const std::regex re(
        R"(\b(?:error|fail|FATAL|FAILED)\b|\b(?:LNK|RC|C)[0-9]{3,5}\b)",
        std::regex::icase | std::regex::optimize);
    return re;
}

// Strip a trailing '\r' (handles CRLF input).
static std::string stripCR(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

// Split input into lines. Preserves whether the input had a trailing newline.
struct SplitResult {
    std::vector<std::string> lines;
    bool trailing_newline = false;
};

static SplitResult splitInput(const std::string& in) {
    SplitResult r;
    if (in.empty()) return r;
    std::size_t start = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\n') {
            r.lines.push_back(stripCR(in.substr(start, i - start)));
            start = i + 1;
        }
    }
    if (start < in.size()) {
        r.lines.push_back(stripCR(in.substr(start)));
        r.trailing_newline = false;
    } else {
        r.trailing_newline = true;
    }
    return r;
}

// Shared-prefix ratio: longest common prefix / min(lenA, lenB).
static double sharedPrefixRatio(const std::string& a, const std::string& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n == 0) return (a.empty() && b.empty()) ? 1.0 : 0.0;
    std::size_t p = 0;
    while (p < n && a[p] == b[p]) ++p;
    return (double)p / (double)n;
}

// Optimised Levenshtein with early-exit on `cap`.
// Returns INT_MAX-ish (cap+1) when exceeded.
static int levenshteinCapped(const std::string& a, const std::string& b, int cap) {
    if (cap < 0) return 0;
    const int la = (int)a.size();
    const int lb = (int)b.size();
    if (std::abs(la - lb) > cap) return cap + 1;
    std::vector<int> prev(lb + 1), cur(lb + 1);
    for (int j = 0; j <= lb; ++j) prev[j] = j;
    for (int i = 1; i <= la; ++i) {
        cur[0] = i;
        int row_min = i;
        for (int j = 1; j <= lb; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({cur[j - 1] + 1,
                                prev[j]     + 1,
                                prev[j - 1] + cost});
            if (cur[j] < row_min) row_min = cur[j];
        }
        if (row_min > cap) return cap + 1;
        std::swap(prev, cur);
    }
    return prev[lb];
}

// Decide whether `b` is a near-duplicate of `a` per opts.
static bool sameOrNear(const std::string& a, const std::string& b, const DedupOpts& opts) {
    if (a == b) return true;
    if (opts.prefix_ratio < 1.0) {
        if (sharedPrefixRatio(a, b) >= opts.prefix_ratio) return true;
    }
    if (opts.max_levenshtein >= 0) {
        if (levenshteinCapped(a, b, opts.max_levenshtein) <= opts.max_levenshtein) return true;
    }
    return false;
}

// Replace literal "{N}" inside marker_format with the count.
static std::string renderMarker(const std::string& fmt, int n) {
    std::string out;
    out.reserve(fmt.size() + 4);
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (i + 2 < fmt.size() && fmt[i] == '{' && fmt[i + 1] == 'N' && fmt[i + 2] == '}') {
            out += std::to_string(n);
            i += 2;
        } else {
            out += fmt[i];
        }
    }
    return out;
}

} // namespace

bool isAlwaysVerbatim(const std::string& line) {
    return std::regex_search(line, verbatimRe());
}

std::string dedupPass(const std::string& in, const DedupOpts& opts) {
    if (in.empty()) return in;
    auto sp = splitInput(in);
    if (sp.lines.empty()) return in;

    std::ostringstream out;

    // Walk lines maintaining a "current run" of near-duplicates.
    const std::size_t N = sp.lines.size();
    std::size_t i = 0;
    while (i < N) {
        const std::string& head = sp.lines[i];
        std::size_t run_end = i + 1;

        // Verbatim allowlist: emit every line of an error/fatal run as-is.
        if (isAlwaysVerbatim(head)) {
            out << head << '\n';
            ++i;
            continue;
        }

        // Extend run while next line matches.
        while (run_end < N && sameOrNear(head, sp.lines[run_end], opts)
               && !isAlwaysVerbatim(sp.lines[run_end])) {
            ++run_end;
        }

        const int run_len = (int)(run_end - i);
        if (run_len >= opts.min_run) {
            out << head << renderMarker(opts.marker_format, run_len) << '\n';
        } else {
            // Below threshold — emit each verbatim.
            for (std::size_t k = i; k < run_end; ++k) {
                out << sp.lines[k] << '\n';
            }
        }
        i = run_end;
    }

    std::string result = out.str();
    // Preserve input trailing-newline behaviour.
    if (!sp.trailing_newline && !result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}

} // namespace icmg::tkil
