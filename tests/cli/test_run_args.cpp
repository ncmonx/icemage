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
