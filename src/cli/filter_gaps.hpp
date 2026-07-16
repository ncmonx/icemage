#pragma once
// Filter-coverage gap detection for `icmg savings` (2026-07-16).
//
// Born from two same-week incidents where a command class was passing through
// Tkil almost entirely UNFILTERED and nobody noticed until a human manually
// eyeballed token_ledger: `git log --oneline` (7-char hash regex miss) and
// `gh api` (no filter registered at all -> 0% saved on 36KB payloads). Both
// were found reactively. This makes `icmg savings` self-diagnosing: it ranks
// the command verbs that burn the most raw bytes while saving the least, so a
// coverage gap surfaces the moment it starts costing tokens -- no human
// suspicion required.
#include "../core/db.hpp"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace icmg::cli {

struct FilterGap {
    std::string verb;        // leading command token(s), e.g. "gh api", "git log"
    int64_t calls = 0;
    int64_t raw_bytes = 0;
    int64_t filtered_bytes = 0;
    double pct_saved = 0.0;  // (raw-filtered)/raw * 100
};

// Extract a stable "verb" grouping key from a command line: the first token,
// plus the second token when the first is a known multi-verb dispatcher
// (git/gh/cargo/npm/etc.) -- so `gh api ...` groups as "gh api", not lumping
// every gh subcommand together. Pure + deterministic.
inline std::string filterGapVerb(const std::string& command) {
    // Skip a leading `icmg run ` wrapper if present.
    std::string c = command;
    auto starts = [&](const char* p) { return c.rfind(p, 0) == 0; };
    if (starts("icmg run ")) c = c.substr(9);
    // Tokenize up to 2 words, honouring a double-quoted first token (so a
    // quoted program path with spaces is not split mid-path).
    std::string first, second;
    size_t i = 0, n = c.size();
    auto skip_ws = [&] { while (i < n && (c[i] == ' ' || c[i] == '\t')) ++i; };
    auto word = [&] {
        std::string w;
        if (i < n && c[i] == '"') {
            ++i;  // consume opening quote
            while (i < n && c[i] != '"') w += c[i++];
            if (i < n) ++i;  // consume closing quote
        } else {
            while (i < n && c[i] != ' ' && c[i] != '\t') w += c[i++];
        }
        return w;
    };
    auto basename = [](std::string p) {
        auto pos = p.find_last_of("/\\");
        return pos == std::string::npos ? p : p.substr(pos + 1);
    };
    skip_ws(); first = word();
    // A path-like verb (contains a separator) -> readable basename.
    if (first.find('/') != std::string::npos || first.find('\\') != std::string::npos)
        return basename(first);
    skip_ws(); second = word();
    static const char* kMultiVerb[] = {"git", "gh", "cargo", "npm", "pnpm",
                                       "yarn", "docker", "kubectl", "dotnet",
                                       "go", "pip", "brew", "apt"};
    for (auto* mv : kMultiVerb) {
        if (first == mv && !second.empty() && second[0] != '-')
            return first + " " + second;
    }
    return first.empty() ? "(empty)" : first;
}

// Rank filter-coverage gaps in a time window: command verbs with the most raw
// bytes and the LEAST proportional savings. `since_ts` = unix-seconds lower
// bound (0 = all time). `min_raw_bytes` filters out trivially-small verbs.
// `max_pct_saved` is the gap threshold (a verb saving >= this is "covered
// enough" and excluded). Returns newest-heaviest first, capped at `limit`.
inline std::vector<FilterGap> findFilterGaps(core::Db& db, int64_t since_ts,
                                             int64_t min_raw_bytes,
                                             double max_pct_saved, int limit) {
    db.run("CREATE TABLE IF NOT EXISTS tool_invocations ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp INTEGER, tool_name TEXT, "
           "command TEXT, raw_bytes INTEGER, filtered_bytes INTEGER, "
           "est_tokens_in INTEGER, est_tokens_out INTEGER, saved_tokens INTEGER)");

    std::unordered_map<std::string, FilterGap> agg;
    std::string sql =
        "SELECT command, COALESCE(raw_bytes,0), COALESCE(filtered_bytes,0) "
        "FROM tool_invocations WHERE COALESCE(raw_bytes,0) > 0";
    std::vector<std::string> params;
    if (since_ts > 0) { sql += " AND timestamp > ?"; params.push_back(std::to_string(since_ts)); }
    db.query(sql, params, [&](const core::Row& r) {
        if (r.size() < 3 || r[0].empty()) return;
        std::string verb = filterGapVerb(r[0]);
        int64_t raw = 0, filt = 0;
        try { raw = std::stoll(r[1]); filt = std::stoll(r[2]); } catch (...) { return; }
        auto& g = agg[verb];
        g.verb = verb;
        g.calls += 1;
        g.raw_bytes += raw;
        g.filtered_bytes += filt;
    });

    std::vector<FilterGap> out;
    for (auto& [k, g] : agg) {
        if (g.raw_bytes < min_raw_bytes) continue;
        g.pct_saved = g.raw_bytes > 0
                          ? (double)(g.raw_bytes - g.filtered_bytes) * 100.0 / (double)g.raw_bytes
                          : 0.0;
        if (g.pct_saved >= max_pct_saved) continue;  // covered enough
        out.push_back(g);
    }
    std::sort(out.begin(), out.end(), [](const FilterGap& a, const FilterGap& b) {
        return a.raw_bytes > b.raw_bytes;  // heaviest waste first
    });
    if (limit > 0 && (int)out.size() > limit) out.resize(limit);
    return out;
}

// Human-actionable recommendation for closing a coverage gap: what kind of
// Tkil filter the verb most likely needs, and where to register it. Pure +
// deterministic so `icmg learn` can print it and a test can pin it. This is
// advisory text, not code generation -- it points a developer at the exact
// next step (the pattern we followed for GhFilter/GitFilter).
inline std::string suggestFilterFor(const FilterGap& g) {
    // A known GitHub-CLI / JSON-API shape -> minify filter (à la GhFilter).
    if (g.verb.rfind("gh ", 0) == 0 || g.verb == "gh" ||
        g.verb.find("curl") != std::string::npos)
        return "register a JSON-minify filter (see src/tkil/filters/gh_filter.cpp): "
               "add a CmdType + detector pattern for `" + g.verb +
               "`, map it to a filter that compacts JSON losslessly";
    // A VCS / dispatcher verb -> line-cap/summary filter (à la GitFilter).
    if (g.verb.rfind("git", 0) == 0 || g.verb.rfind("docker", 0) == 0 ||
        g.verb.rfind("kubectl", 0) == 0)
        return "extend the matching filter (e.g. src/tkil/filters/git_filter.cpp) "
               "to cap/summarise `" + g.verb + "` output";
    // Generic large-output verb -> a default line/byte cap filter.
    return "add a Tkil filter for `" + g.verb +
           "`: a CmdType + detector pattern + a filter that caps or summarises "
           "its output (see src/tkil/filters/ for the pattern)";
}

// Serialise gaps as a JSON array for `icmg savings --json` (badge/CI/tooling
// consumers). Pure + deterministic; escapes the verb (may contain quotes or
// backslashes from a Windows path). 2026-07-16: JSON output had no gap field
// even though the human console output did -- so automation couldn't act on a
// coverage hole without re-parsing prose.
inline std::string filterGapsJson(const std::vector<FilterGap>& gaps) {
    auto esc = [](const std::string& s) {
        std::string o;
        for (char ch : s) {
            if (ch == '"' || ch == '\\') o += '\\';
            o += ch;
        }
        return o;
    };
    std::string out = "[";
    for (size_t i = 0; i < gaps.size(); ++i) {
        const auto& g = gaps[i];
        if (i) out += ",";
        out += "{\"verb\":\"" + esc(g.verb) + "\",\"calls\":" + std::to_string(g.calls) +
               ",\"raw_bytes\":" + std::to_string(g.raw_bytes) +
               ",\"filtered_bytes\":" + std::to_string(g.filtered_bytes) +
               ",\"pct_saved\":" + std::to_string((int)(g.pct_saved + 0.5)) + "}";
    }
    out += "]";
    return out;
}

}  // namespace icmg::cli
