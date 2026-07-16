#include "../test_main.hpp"
#include "../../src/cli/filter_gaps.hpp"
#include "../../src/core/db.hpp"
#include <string>

using icmg::cli::filterGapVerb;
using icmg::cli::findFilterGaps;
using icmg::cli::suggestFilterFor;
using icmg::cli::filterGapsJson;
using icmg::cli::FilterGap;

// --- verb extraction --------------------------------------------------------
TEST("filterGapVerb: plain single-verb command -> first token") {
    ASSERT_EQ(filterGapVerb("cat somefile.txt"), std::string("cat"));
    ASSERT_EQ(filterGapVerb("ls -la"), std::string("ls"));
}

TEST("filterGapVerb: multi-verb dispatcher keeps the subcommand (gh api, git log)") {
    ASSERT_EQ(filterGapVerb("gh api gists/abc123"), std::string("gh api"));
    ASSERT_EQ(filterGapVerb("git log --oneline -5"), std::string("git log"));
    ASSERT_EQ(filterGapVerb("docker ps -a"), std::string("docker ps"));
}

TEST("filterGapVerb: multi-verb with a flag as 2nd token does NOT glue the flag") {
    ASSERT_EQ(filterGapVerb("git --version"), std::string("git"));
}

TEST("filterGapVerb: strips a leading `icmg run ` wrapper") {
    ASSERT_EQ(filterGapVerb("icmg run gh api repos/x/y"), std::string("gh api"));
}

TEST("filterGapVerb: a quoted program path with spaces is not split mid-path; shown as basename") {
    // Regression: real telemetry produced the mangled verb `"C:\Program`
    // from a command like "C:\Program Files\Foo\bar.exe" --flag.
    ASSERT_EQ(filterGapVerb("\"C:\\Program Files\\Foo\\bar.exe\" --flag"),
              std::string("bar.exe"));
}

TEST("filterGapVerb: an unquoted absolute/relative path verb is reduced to its basename") {
    ASSERT_EQ(filterGapVerb("/usr/local/bin/tool -x"), std::string("tool"));
    ASSERT_EQ(filterGapVerb("./scripts/run.sh"), std::string("run.sh"));
}

// --- gap ranking ------------------------------------------------------------
TEST("findFilterGaps: empty table yields no gaps") {
    icmg::core::Db db(":memory:");
    auto gaps = findFilterGaps(db, 0, 0, 100.0, 10);
    ASSERT_EQ((int)gaps.size(), 0);
}

static void ins(icmg::core::Db& db, const std::string& cmd, int64_t raw, int64_t filt) {
    db.run("CREATE TABLE IF NOT EXISTS tool_invocations ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp INTEGER, tool_name TEXT, "
           "command TEXT, raw_bytes INTEGER, filtered_bytes INTEGER, "
           "est_tokens_in INTEGER, est_tokens_out INTEGER, saved_tokens INTEGER)");
    db.run("INSERT INTO tool_invocations(timestamp,tool_name,command,raw_bytes,filtered_bytes,saved_tokens)"
           " VALUES (?,?,?,?,?,?)",
           {std::to_string((int64_t)std::time(nullptr)), "bash", cmd,
            std::to_string(raw), std::to_string(filt), std::to_string((raw - filt) / 4)});
}

TEST("findFilterGaps: an uncovered high-volume verb (0% saved) is surfaced as the top gap") {
    icmg::core::Db db(":memory:");
    // gh api: 40000 raw -> 40000 filtered (0% saved) -- the exact real bug.
    ins(db, "gh api gists/abc", 40000, 40000);
    // git log: 8000 raw -> 1600 filtered (80% saved) -- well covered.
    ins(db, "git log --oneline", 8000, 1600);
    auto gaps = findFilterGaps(db, 0, 1000, 15.0, 10);
    ASSERT_EQ((int)gaps.size(), 1);                 // only the uncovered one
    ASSERT_EQ(gaps[0].verb, std::string("gh api"));
    ASSERT_TRUE(gaps[0].pct_saved < 1.0);
}

TEST("findFilterGaps: multiple calls of the same verb aggregate into one row") {
    icmg::core::Db db(":memory:");
    ins(db, "gh api a", 10000, 10000);
    ins(db, "gh api b", 10000, 10000);
    ins(db, "gh api c", 10000, 10000);
    auto gaps = findFilterGaps(db, 0, 1000, 15.0, 10);
    ASSERT_EQ((int)gaps.size(), 1);
    ASSERT_EQ((long long)gaps[0].calls, 3LL);
    ASSERT_EQ((long long)gaps[0].raw_bytes, 30000LL);
}

TEST("findFilterGaps: a well-covered verb above the threshold is excluded") {
    icmg::core::Db db(":memory:");
    ins(db, "git diff", 50000, 5000);  // 90% saved -> covered, excluded
    auto gaps = findFilterGaps(db, 0, 1000, 15.0, 10);
    ASSERT_EQ((int)gaps.size(), 0);
}

TEST("findFilterGaps: min_raw_bytes filters out trivially small verbs") {
    icmg::core::Db db(":memory:");
    ins(db, "gh api tiny", 200, 200);  // uncovered but tiny -> below floor
    auto gaps = findFilterGaps(db, 0, 1000, 15.0, 10);
    ASSERT_EQ((int)gaps.size(), 0);
}

TEST("findFilterGaps: results are ranked heaviest-raw-bytes first, capped at limit") {
    icmg::core::Db db(":memory:");
    ins(db, "gh api x", 30000, 30000);
    ins(db, "curl y", 50000, 50000);
    ins(db, "wget z", 10000, 10000);
    auto gaps = findFilterGaps(db, 0, 1000, 15.0, 2);
    ASSERT_EQ((int)gaps.size(), 2);                     // capped
    ASSERT_EQ(gaps[0].verb, std::string("curl"));      // heaviest first
    ASSERT_EQ(gaps[1].verb, std::string("gh api"));
}

// --- filter suggestions -----------------------------------------------------
TEST("suggestFilterFor: a gh/curl JSON verb recommends a JSON-minify filter") {
    FilterGap g; g.verb = "gh api";
    auto s = suggestFilterFor(g);
    ASSERT_CONTAINS(s, "JSON-minify");
    ASSERT_CONTAINS(s, "gh api");
}

TEST("suggestFilterFor: a git/docker dispatcher verb recommends extending the matching filter") {
    FilterGap g; g.verb = "git blame";
    auto s = suggestFilterFor(g);
    ASSERT_CONTAINS(s, "git_filter.cpp");
    ASSERT_CONTAINS(s, "git blame");
}

TEST("suggestFilterFor: an unknown verb recommends a generic cap/summarise filter") {
    FilterGap g; g.verb = "somerandomtool";
    auto s = suggestFilterFor(g);
    ASSERT_CONTAINS(s, "somerandomtool");
    ASSERT_CONTAINS(s, "src/tkil/filters/");
}

// --- json serialisation -----------------------------------------------------
TEST("filterGapsJson: empty gaps -> empty JSON array") {
    ASSERT_EQ(filterGapsJson({}), std::string("[]"));
}

TEST("filterGapsJson: a gap serialises all fields") {
    FilterGap g; g.verb = "gh api"; g.calls = 3; g.raw_bytes = 77000;
    g.filtered_bytes = 77000; g.pct_saved = 0.0;
    auto j = filterGapsJson({g});
    ASSERT_CONTAINS(j, "\"verb\":\"gh api\"");
    ASSERT_CONTAINS(j, "\"calls\":3");
    ASSERT_CONTAINS(j, "\"raw_bytes\":77000");
    ASSERT_CONTAINS(j, "\"pct_saved\":0");
}

TEST("filterGapsJson: a verb containing a backslash/quote is escaped (valid JSON)") {
    FilterGap g; g.verb = "C:\\a\\b.exe"; g.calls = 1; g.raw_bytes = 30000;
    auto j = filterGapsJson({g});
    // backslashes doubled -> no raw single backslash before a non-escape char
    ASSERT_CONTAINS(j, "C:\\\\a\\\\b.exe");
}

TEST("filterGapsJson: multiple gaps are comma-separated inside one array") {
    FilterGap a; a.verb = "gh api"; a.calls = 1; a.raw_bytes = 30000;
    FilterGap b; b.verb = "curl"; b.calls = 1; b.raw_bytes = 40000;
    auto j = filterGapsJson({a, b});
    ASSERT_TRUE(j.front() == '[' && j.back() == ']');
    ASSERT_CONTAINS(j, "},{");
}
