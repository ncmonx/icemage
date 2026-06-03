// v2.0.0 C6: structurally-lossless trim — drop blank/boilerplate/duplicate lines, keep order.
#include "../test_main.hpp"
#include "../../src/core/structural_trim.hpp"
#include <string>
using namespace icmg::core;

TEST("isBoilerplateLine: blank + punctuation-only true; content false") {
    ASSERT_TRUE(isBoilerplateLine(""));
    ASSERT_TRUE(isBoilerplateLine("    "));
    ASSERT_TRUE(isBoilerplateLine("--------"));
    ASSERT_TRUE(isBoilerplateLine("});"));
    ASSERT_TRUE(!isBoilerplateLine("error: build failed"));
    ASSERT_TRUE(!isBoilerplateLine("  x = 1"));
}

TEST("structuralTrim: drops blank + boilerplate, keeps content order") {
    std::string in = "first line\n\n------\nsecond line\n";
    ASSERT_EQ(structuralTrim(in), std::string("first line\nsecond line"));
}

TEST("structuralTrim: drops exact duplicate lines (first kept)") {
    std::string in = "alpha\nbeta\nalpha\ngamma\nbeta";
    ASSERT_EQ(structuralTrim(in), std::string("alpha\nbeta\ngamma"));
}

TEST("structuralTrim: CRLF tolerated") {
    std::string in = "one\r\ntwo\r\n";
    ASSERT_EQ(structuralTrim(in), std::string("one\ntwo"));
}

TEST("structuralTrim: empty -> empty") {
    ASSERT_EQ(structuralTrim(""), std::string(""));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
