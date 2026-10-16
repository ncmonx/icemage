// wflog #16755 (2026-10-16): safeExecWin built the child cmdline with MSVCRT
// (CommandLineToArgvW) quoting for EVERY child -- but MSYS/Cygwin children
// (Git Bash usr/bin tools: grep, find, sed, bash itself) re-split their
// cmdline with DIFFERENT rules: inside quotes `\\` -> `\` and `\"` -> `"`.
// An MSVCRT-quoted arg therefore lost one backslash of every pair before an
// MSYS child parsed it. Same quoting rules as the companion agent harness: quote per
// child flavor (quoteArgMsvcrt / quoteArgMsys / isMsysProgram).

#include "../test_main.hpp"
#include "../../src/core/msys_quote.hpp"

#include <string>

using icmg::core::quoteArgMsvcrt;
using icmg::core::quoteArgMsys;
using icmg::core::isMsysProgram;
using icmg::core::buildChildCmdline;

TEST("msvcrt: plain arg stays bare") {
    ASSERT_EQ(quoteArgMsvcrt("abc"), std::string("abc"));
}
TEST("msvcrt: space arg quoted, inner backslashes literal") {
    ASSERT_EQ(quoteArgMsvcrt("a b"), std::string("\"a b\""));
    ASSERT_EQ(quoteArgMsvcrt("C:\\dir x\\f"), std::string("\"C:\\dir x\\f\""));
}
TEST("msvcrt: backslashes before an embedded quote are doubled + escaped") {
    // a\"b -> "a\\\"b"
    ASSERT_EQ(quoteArgMsvcrt("a\\\"b"), std::string("\"a\\\\\\\"b\""));
}
TEST("msvcrt: trailing backslash in quoted arg doubled") {
    ASSERT_EQ(quoteArgMsvcrt("a \\"), std::string("\"a \\\\\""));
}

TEST("msys: backslash arg always quoted, backslash escaped") {
    // a\b -> "a\\b"
    ASSERT_EQ(quoteArgMsys("a\\b"), std::string("\"a\\\\b\""));
}
TEST("msys: embedded quote escaped") {
    // a"b -> "a\"b"
    ASSERT_EQ(quoteArgMsys("a\"b"), std::string("\"a\\\"b\""));
}
TEST("msys: plain arg stays bare") {
    ASSERT_EQ(quoteArgMsys("abc"), std::string("abc"));
}

TEST("isMsysProgram: git usr-bin, msys64, bare bash -> yes") {
    ASSERT_TRUE(isMsysProgram("C:\\Program Files\\Git\\usr\\bin\\grep.exe"));
    ASSERT_TRUE(isMsysProgram("C:/msys64/usr/bin/sed.exe"));
    ASSERT_TRUE(isMsysProgram("C:/Program Files/Git/bin/bash.exe"));
    ASSERT_TRUE(isMsysProgram("bash.exe"));
    ASSERT_TRUE(isMsysProgram("bash"));
}
TEST("isMsysProgram: native windows binaries -> no") {
    ASSERT_FALSE(isMsysProgram("C:\\Windows\\System32\\bash.exe"));  // WSL shim
    ASSERT_FALSE(isMsysProgram("C:\\Windows\\System32\\cmd.exe"));
    ASSERT_FALSE(isMsysProgram("C:/Windows/System32/findstr.exe"));
    ASSERT_FALSE(isMsysProgram("node.exe"));
}

TEST("buildChildCmdline: msys child gets msys quoting (backslash survives)") {
    // arg a\b to an msys grep -> "a\\b" so the child recovers a\b
    auto s = buildChildCmdline({"C:/msys64/usr/bin/grep.exe", "a\\b"});
    ASSERT_EQ(s, std::string("C:/msys64/usr/bin/grep.exe \"a\\\\b\""));
}
TEST("buildChildCmdline: native child keeps msvcrt rules") {
    auto s = buildChildCmdline({"C:/Windows/System32/findstr.exe", "a\\b"});
    ASSERT_EQ(s, std::string("C:/Windows/System32/findstr.exe a\\b"));
}
