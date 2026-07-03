// tests/tkil/test_output_nano.cpp
// D1 nano mode — unit tests for symbol-only compression.
// Uses the icmg test framework (TEST/ASSERT_*); mono_main provides main().
#include "../test_main.hpp"
#include "../../src/tkil/output_nano.hpp"

using icmg::tkil::NanoEntry;
using icmg::tkil::parseNanoLine;
using icmg::tkil::nanoCompress;

// --- parseNanoLine: clang/gcc error (no code) ---
TEST("nano: clang error parses, no code -> '-'") {
    NanoEntry e;
    ASSERT_TRUE(parseNanoLine("src/main.cpp:12:5: error: 'x' was not declared", e));
    ASSERT_EQ(e.file, std::string("src/main.cpp"));
    ASSERT_EQ(e.kind, std::string("err"));
    ASSERT_EQ(e.code, std::string("-"));
    ASSERT_EQ(e.line, std::string("12"));
}

// --- rustc error WITH code ---
TEST("nano: rustc error[CODE] parses code") {
    NanoEntry e;
    ASSERT_TRUE(parseNanoLine("src/main.rs:12:9: error[E0423]: expected value", e));
    ASSERT_EQ(e.code, std::string("E0423"));
    ASSERT_EQ(e.kind, std::string("err"));
    ASSERT_EQ(e.line, std::string("12"));
}

// --- gcc warning (no col) ---
TEST("nano: gcc warning without column parses") {
    NanoEntry e;
    ASSERT_TRUE(parseNanoLine("foo.c:45: warning: unused variable 'y'", e));
    ASSERT_EQ(e.kind, std::string("warn"));
    ASSERT_EQ(e.line, std::string("45"));
}

// --- MSVC error with code ---
TEST("nano: msvc error parses code+line") {
    NanoEntry e;
    ASSERT_TRUE(parseNanoLine("foo.cpp(12): error C2065: 'x': undeclared identifier", e));
    ASSERT_EQ(e.file, std::string("foo.cpp"));
    ASSERT_EQ(e.code, std::string("C2065"));
    ASSERT_EQ(e.kind, std::string("err"));
    ASSERT_EQ(e.line, std::string("12"));
}

// --- MSVC warning with column ---
TEST("nano: msvc warning with column parses") {
    NanoEntry e;
    ASSERT_TRUE(parseNanoLine("bar.cpp(88,4): warning C4996: deprecated", e));
    ASSERT_EQ(e.code, std::string("C4996"));
    ASSERT_EQ(e.kind, std::string("warn"));
}

// --- non-diagnostic lines do NOT parse ---
TEST("nano: plain lines do not parse") {
    NanoEntry e;
    ASSERT_FALSE(parseNanoLine("Compiling my_crate v0.1.0", e));
    ASSERT_FALSE(parseNanoLine("   Finished dev [unoptimized]", e));
}

// --- nanoCompress: full render + summary ---
TEST("nano: compress renders symbols + summary counts") {
    std::string out = nanoCompress(
        "Compiling foo v0.1.0\n"
        "src/main.rs:12:9: error[E0423]: expected value\n"
        "src/lib.rs:3:1: warning: unused import\n"
        "error: aborting due to previous error\n");
    ASSERT_CONTAINS(out, "src/main.rs:err:E0423:12");
    ASSERT_CONTAINS(out, "src/lib.rs:warn:-:3");
    ASSERT_CONTAINS(out, "1 err");
    ASSERT_CONTAINS(out, "1 warn");
}

// --- nanoCompress: dedup identical diagnostics ---
TEST("nano: compress dedups identical diagnostics") {
    std::string out = nanoCompress(
        "a.cpp(5): error C2065: x\n"
        "a.cpp(5): error C2065: x\n");
    ASSERT_CONTAINS(out, "a.cpp:err:C2065:5");
    // Counted once despite two identical input lines.
    ASSERT_CONTAINS(out, "1 err");
}

// --- nanoCompress: no diagnostics -> informational line ---
TEST("nano: compress with no diagnostics is informational") {
    std::string out = nanoCompress("all good\nrunning tests\nok\n");
    ASSERT_CONTAINS(out, "no diagnostics");
    ASSERT_CONTAINS(out, "3 line");
}
