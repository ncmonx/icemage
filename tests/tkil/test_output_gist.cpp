// tests/tkil/test_output_gist.cpp
// D4 semantic gisting — one-line TL;DR of command output.
// Uses the icmg test framework (TEST/ASSERT_*); mono_main provides main().
#include "../test_main.hpp"
#include "../../src/tkil/output_gist.hpp"

using icmg::tkil::GistKind;
using icmg::tkil::gistClassify;
using icmg::tkil::gistOutput;

// --- classification ---
TEST("gist: classify git diff / log / test / build") {
    ASSERT_TRUE(gistClassify("git diff HEAD~1", "") == GistKind::Diff);
    ASSERT_TRUE(gistClassify("git log --oneline -5", "") == GistKind::Log);
    ASSERT_TRUE(gistClassify("cargo test", "") == GistKind::Test);
    ASSERT_TRUE(gistClassify("cmake --build build", "") == GistKind::Build);
    ASSERT_TRUE(gistClassify("echo hi", "hi\n") == GistKind::Generic);
}

// --- test gist: pass/fail counts + first failure location ---
TEST("gist: test summary counts + first fail location") {
    std::string out =
        "running 15 tests\n"
        "test user::auth ... FAILED\n"
        "thread 'user' panicked at src/user.rs:45\n"
        "test result: FAILED. 12 passed; 3 failed;\n";
    std::string g = gistOutput("cargo test", 1, out);
    ASSERT_CONTAINS(g, "12 passed");
    ASSERT_CONTAINS(g, "3 failed");
    ASSERT_CONTAINS(g, "src/user.rs:45");
}

// --- test gist: all-pass, no fail location ---
TEST("gist: all-pass test summary") {
    std::string out = "test result: ok. 20 passed; 0 failed;\n";
    std::string g = gistOutput("ctest", 0, out);
    ASSERT_CONTAINS(g, "20 passed");
    ASSERT_CONTAINS(g, "0 failed");
    ASSERT_NOT_CONTAINS(g, "first fail");
}

// --- diff gist: +/- and file count ---
TEST("gist: diff line + file counts") {
    std::string out =
        "diff --git a/x.c b/x.c\n"
        "--- a/x.c\n"
        "+++ b/x.c\n"
        "+added one\n"
        "+added two\n"
        "-removed one\n"
        "diff --git a/y.c b/y.c\n"
        "--- a/y.c\n"
        "+++ b/y.c\n"
        "+another add\n";
    std::string g = gistOutput("git diff", 0, out);
    ASSERT_CONTAINS(g, "+3");
    ASSERT_CONTAINS(g, "-1");
    ASSERT_CONTAINS(g, "2 file");
}

// --- log gist: commit count + subjects ---
TEST("gist: log commit count + first subjects") {
    std::string out =
        "a1b2c3d feat(auth): refresh token\n"
        "e4f5a6b fix: null pointer in parser\n"
        "9988776 chore: bump deps\n";
    std::string g = gistOutput("git log --oneline", 0, out);
    ASSERT_CONTAINS(g, "3 commit");
    ASSERT_CONTAINS(g, "feat(auth): refresh token");
}

// --- build gist: error/warning counts ---
TEST("gist: build error + warning counts") {
    std::string out =
        "compiling foo\n"
        "main.cpp(12): error C2065: undeclared\n"
        "main.cpp(20): warning C4996: deprecated\n"
        "util.cpp(5): warning C4244: conversion\n";
    std::string g = gistOutput("cmake --build build", 1, out);
    ASSERT_CONTAINS(g, "1 error");
    ASSERT_CONTAINS(g, "2 warning");
}

// --- generic fallback: line count + exit ---
TEST("gist: generic fallback reports lines + exit") {
    std::string out = "alpha\nbeta\ngamma\n";
    std::string g = gistOutput("echo stuff", 0, out);
    ASSERT_CONTAINS(g, "3 line");
    ASSERT_CONTAINS(g, "exit 0");
}
