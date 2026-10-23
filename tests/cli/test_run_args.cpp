// v1.70.0 (#178): icmg run flag parsing — leading-flag-only + "--" passthrough.

#include "../test_main.hpp"
#include "../../src/cli/run_args.hpp"

#include <string>
#include <vector>

using icmg::cli::parseRunArgs;

TEST("run-args: leading icmg flags consumed, child flags pass through") {
    auto r = parseRunArgs({"--raw", "./tool", "--json", "-p", "hi"});
    ASSERT_TRUE(r.raw);
    ASSERT_FALSE(r.json_out);                 // --json belongs to the child, not icmg
    ASSERT_EQ(r.command, std::string("./tool --json -p hi"));
}

TEST("run-args: bare -- passes everything verbatim, marker dropped") {
    auto r = parseRunArgs({"--", "./mytool", "--json"});
    ASSERT_FALSE(r.json_out);                 // not eaten
    ASSERT_EQ(r.command, std::string("./mytool --json"));
    ASSERT_EQ(r.cmd_args.size(), (size_t)2);  // -- itself is dropped
    ASSERT_EQ(r.cmd_args[0], std::string("./mytool"));
}

TEST("run-args: first non-flag stops icmg flag parsing") {
    auto r = parseRunArgs({"./tool", "--json", "--raw"});
    ASSERT_FALSE(r.json_out);                 // child flag
    ASSERT_FALSE(r.raw);                      // also child (after command start)
    ASSERT_EQ(r.command, std::string("./tool --json --raw"));
}

TEST("run-args: icmg flags before command still apply") {
    auto r = parseRunArgs({"--json", "--ultra", "git", "status"});
    ASSERT_TRUE(r.json_out);
    ASSERT_TRUE(r.ultra);
    ASSERT_EQ(r.command, std::string("git status"));
}

TEST("run-args: args with spaces are re-quoted") {
    auto r = parseRunArgs({"echo", "hello world"});
    ASSERT_CONTAINS(r.command, "\"hello world\"");
}
TEST("run-args: single quoted token is a verbatim shell line (pipe preserved)") {
    auto r = parseRunArgs({"ls src/ | grep -i graph"});
    ASSERT_EQ(r.command, std::string("ls src/ | grep -i graph"));   // NOT re-quoted
}

TEST("run-args: single token with spaces runs as a shell line") {
    auto r = parseRunArgs({"echo hello"});
    ASSERT_EQ(r.command, std::string("echo hello"));
}

TEST("run-args: single token after --raw is still a verbatim shell line") {
    auto r = parseRunArgs({"--raw", "ls | wc -l"});
    ASSERT_TRUE(r.raw);
    ASSERT_EQ(r.command, std::string("ls | wc -l"));
}

using icmg::cli::destructiveDecision;
using icmg::cli::DestructiveDecision;

TEST("destructive: non-destructive always proceeds") {
    ASSERT_TRUE(destructiveDecision(false,false,false,false,true) == DestructiveDecision::Proceed);
}
TEST("destructive: safe dir proceeds even if destructive") {
    ASSERT_TRUE(destructiveDecision(false,false,true,true,true) == DestructiveDecision::Proceed);
}
TEST("destructive: --yes or env proceeds") {
    ASSERT_TRUE(destructiveDecision(true,false,true,false,true) == DestructiveDecision::Proceed);
    ASSERT_TRUE(destructiveDecision(false,true,true,false,true) == DestructiveDecision::Proceed);
}
TEST("destructive: non-interactive (no tty) auto-denies, never prompts (#184)") {
    ASSERT_TRUE(destructiveDecision(false,false,true,false,false) == DestructiveDecision::Deny);
}
TEST("destructive: interactive tty prompts") {
    ASSERT_TRUE(destructiveDecision(false,false,true,false,true) == DestructiveDecision::Prompt);
}

// v2.20.0: argv-aware destructive detection. The old substring scan flagged any
// command line CONTAINING "rm " + "-f"/"-r" anywhere -- so `grep 'rm -rf'`, a
// path like `src/farm/`, or a search pattern falsely tripped the guard. The new
// detector only flags when the LEADING verb is the destructive tool.
using icmg::cli::isDestructiveArgv;

TEST("destructive-argv: real rm -rf is flagged") {
    std::string why;
    ASSERT_TRUE(isDestructiveArgv({"rm", "-rf", "build/"}, why));
    ASSERT_EQ(why, std::string("rm with -r/-f"));
}
TEST("destructive-argv: rm -f is flagged") {
    std::string why;
    ASSERT_TRUE(isDestructiveArgv({"rm", "-f", "a.txt"}, why));
}
TEST("destructive-argv: grep for 'rm -rf' is NOT flagged (false-positive fix)") {
    std::string why;
    ASSERT_FALSE(isDestructiveArgv({"grep", "rm -rf", "notes.txt"}, why));
}
TEST("destructive-argv: path containing 'rm' is NOT flagged") {
    std::string why;
    ASSERT_FALSE(isDestructiveArgv({"ls", "-la", "src/farm/-r-thing"}, why));
}
TEST("destructive-argv: single-token shell line still parses leading verb") {
    std::string why;
    ASSERT_TRUE(isDestructiveArgv({"rm -rf /tmp/x"}, why));         // one quoted token
    std::string why2;
    ASSERT_FALSE(isDestructiveArgv({"echo rm -rf danger"}, why2));  // echo is leading
}
TEST("destructive-argv: Remove-Item / git rm -r / rmdir /s / SQL still caught") {
    std::string w;
    ASSERT_TRUE(isDestructiveArgv({"Remove-Item", "x", "-Recurse"}, w));
    std::string w2; ASSERT_TRUE(isDestructiveArgv({"git", "rm", "-r", "dir"}, w2));
    std::string w3; ASSERT_TRUE(isDestructiveArgv({"rmdir", "/s", "dir"}, w3));
    std::string w4; ASSERT_TRUE(isDestructiveArgv({"psql", "-c", "DROP TABLE t"}, w4));
    std::string w5; ASSERT_TRUE(isDestructiveArgv({"git", "reset", "--hard"}, w5));
}
TEST("destructive-argv: git rm WITHOUT -r/-f is NOT auto-flagged") {
    std::string w;
    ASSERT_FALSE(isDestructiveArgv({"git", "rm", "--cached", "a.txt"}, w));
}

// Root-cause #2: env-prefix bypass. A leading `ICMG_ASSUME_YES=1` / `FORCE=1`
// on the wrapped command line should be honored as bypass intent.
using icmg::cli::hasInlineYesPrefix;

TEST("inline-yes: ICMG_ASSUME_YES=1 prefix is honored") {
    ASSERT_TRUE(hasInlineYesPrefix({"ICMG_ASSUME_YES=1", "rm", "-rf", "x"}));
    ASSERT_TRUE(hasInlineYesPrefix({"ICMG_ASSUME_YES=1 rm -rf x"}));  // single token
}
TEST("inline-yes: no prefix -> false") {
    ASSERT_FALSE(hasInlineYesPrefix({"rm", "-rf", "x"}));
    ASSERT_FALSE(hasInlineYesPrefix({"FOO=1", "rm", "-rf", "x"}));   // unrelated var
    ASSERT_FALSE(hasInlineYesPrefix({"ICMG_ASSUME_YES=0", "rm", "x"}));
}

// ---------------------------------------------------------------------------
// wflog #16755 (2026-10-16): `icmg run` spawned bash with the OLD quoting --
// runQuoteArg escaped only `"`, so a token holding `\` went through unquoted
// (bash ate one backslash per pair: `a\\b` -> `a\b`, `C:\dir\f` -> `C:dirf`)
// and a `\"` pair re-opened/closed the quote early. Must follow the same MSYS
// rule as the companion agent harness quote_arg_msys: escape `\` AND `"`.
// Plus: anti-nest guard -- `icmg run icmg run <cmd>` must unwrap, not re-wrap.
// ---------------------------------------------------------------------------
using icmg::cli::runQuoteArg;

TEST("quote-msys: backslash token is quoted + backslash-escaped") {
    // token a\\b (two literal backslashes) -> "a\\\\b"
    ASSERT_EQ(runQuoteArg("a\\\\b"), std::string("\"a\\\\\\\\b\""));
}
TEST("quote-msys: backslash+quote pair does not close the quote early") {
    // token say\"hi -> "say\\\"hi"
    ASSERT_EQ(runQuoteArg("say\\\"hi"), std::string("\"say\\\\\\\"hi\""));
}
TEST("quote-msys: windows path keeps its backslashes through bash -c") {
    auto r = parseRunArgs({"type", "C:\\dir\\file.txt"});
    ASSERT_CONTAINS(r.command, "\"C:\\\\dir\\\\file.txt\"");
}
TEST("quote-msys: plain token still unquoted (no churn)") {
    ASSERT_EQ(runQuoteArg("abc"), std::string("abc"));
    ASSERT_EQ(runQuoteArg("hello world"), std::string("\"hello world\""));
}

TEST("anti-nest: `icmg run <cmd>` argv is unwrapped") {
    auto r = parseRunArgs({"icmg", "run", "echo", "hi"});
    ASSERT_EQ(r.command, std::string("echo hi"));
}
TEST("anti-nest: inner icmg-run flags are merged, not executed") {
    auto r = parseRunArgs({"icmg", "run", "--raw", "echo", "hi"});
    ASSERT_TRUE(r.raw);
    ASSERT_EQ(r.command, std::string("echo hi"));
}
TEST("anti-nest: doubly nested unwraps fully") {
    auto r = parseRunArgs({"icmg", "run", "icmg", "run", "echo", "hi"});
    ASSERT_EQ(r.command, std::string("echo hi"));
}
TEST("anti-nest: full-path icmg.exe wrapper also unwrapped") {
    auto r = parseRunArgs({"C:\\bin\\icmg.exe", "run", "echo", "hi"});
    ASSERT_EQ(r.command, std::string("echo hi"));
}
TEST("anti-nest: single-token nested shell line loses the wrapper") {
    auto r = parseRunArgs({"icmg run ls | grep x"});
    ASSERT_EQ(r.command, std::string("ls | grep x"));
}
TEST("anti-nest: other icmg subcommands are NOT unwrapped") {
    auto r = parseRunArgs({"icmg", "recall", "x"});
    ASSERT_EQ(r.command, std::string("icmg recall x"));
}

// -- v2.26.0: patch-risk scoring + txn state helpers --------------------------
#include "../../src/cli/test_risk.hpp"
#include "../../src/cli/txn_guard.hpp"
#include <set>

TEST("risk: impact count maps to level") {
    using icmg::cli::riskLevel;
    ASSERT_EQ(riskLevel(30, "src/x.cpp"), std::string("HIGH"));
    ASSERT_EQ(riskLevel(10, "src/x.cpp"), std::string("MED"));
    ASSERT_EQ(riskLevel(2,  "src/x.cpp"), std::string("LOW"));
}

TEST("risk: shared core header is HIGH regardless of count") {
    ASSERT_EQ(icmg::cli::riskLevel(0, "src/core/db.hpp"), std::string("HIGH"));
}

TEST("risk: ctest regex from selected test files") {
    std::set<std::string> t{"tests/cli/test_a.cpp", "tests/x/test_b.cpp"};
    ASSERT_EQ(icmg::cli::ctestRegexFor(t), std::string("(test_a|test_b)"));
}

TEST("txn: state roundtrip") {
    icmg::cli::TxnState s; s.tree = "abc123"; s.ts = 1700000000;
    auto parsed = icmg::cli::parseTxnState(icmg::cli::serializeTxnState(s));
    ASSERT_EQ(parsed.tree, std::string("abc123"));
    ASSERT_TRUE(parsed.ts == 1700000000);
}

TEST("txn: parse garbage yields empty state") {
    ASSERT_TRUE(icmg::cli::parseTxnState("not json").tree.empty());
}

TEST("txn: state path lands under .icmg") {
    auto p = icmg::cli::txnStatePathFor("/repo");
    ASSERT_TRUE(p.find(".icmg/txn-state.json") != std::string::npos);
}
