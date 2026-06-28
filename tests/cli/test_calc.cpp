// `icmg calc` pure evaluator unit tests (2026-06-28).
// Spec for the expression evaluator that closes the python-throwaway RAW=1 hole.
#include "../test_main.hpp"
#include "../../src/cli/calc_eval.hpp"

using namespace icmg::cli;

TEST("calc: basic addition") {
    auto r = evalExpr("2+3");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 5.0);
}

TEST("calc: precedence mul over add") {
    auto r = evalExpr("2+3*4");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 14.0);
}

TEST("calc: parentheses override precedence") {
    auto r = evalExpr("(2+3)*4");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 20.0);
}

TEST("calc: power is right-associative") {
    auto r = evalExpr("2**3**2");   // 2**(3**2) = 2**9 = 512
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 512.0);
}

TEST("calc: 2**32 big int") {
    auto r = evalExpr("2**32");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 4294967296.0);
}

TEST("calc: unary minus") {
    auto r = evalExpr("-5+3");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, -2.0);
}

TEST("calc: modulo") {
    auto r = evalExpr("10%3");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 1.0);
}

TEST("calc: percentage expression") {
    auto r = evalExpr("50/200*100");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 25.0);
}

TEST("calc: sqrt function") {
    auto r = evalExpr("sqrt(144)");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 12.0);
}

TEST("calc: avg variadic") {
    auto r = evalExpr("avg(1,2,3,4)");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 2.5);
}

TEST("calc: sum variadic") {
    auto r = evalExpr("sum(1,2,3,4)");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 10.0);
}

TEST("calc: min/max") {
    ASSERT_EQ(evalExpr("min(5,2,8)").value, 2.0);
    ASSERT_EQ(evalExpr("max(5,2,8)").value, 8.0);
}

TEST("calc: nested functions") {
    auto r = evalExpr("max(sqrt(16), avg(2,4,6))");  // max(4, 4) = 4
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 4.0);
}

TEST("calc: pi constant") {
    auto r = evalExpr("pi");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(r.value > 3.14 && r.value < 3.15);
}

TEST("calc: whitespace tolerant") {
    auto r = evalExpr("  2  +   3 * 4 ");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 14.0);
}

TEST("calc: error on trailing operator") {
    auto r = evalExpr("2+");
    ASSERT_FALSE(r.ok);
    ASSERT_CONTAINS(r.error, "end");
}

TEST("calc: error on division by zero") {
    auto r = evalExpr("1/0");
    ASSERT_FALSE(r.ok);
    ASSERT_CONTAINS(r.error, "division by zero");
}

TEST("calc: error on empty") {
    auto r = evalExpr("   ");
    ASSERT_FALSE(r.ok);
    ASSERT_CONTAINS(r.error, "empty");
}

TEST("calc: error on unknown name") {
    auto r = evalExpr("foo");
    ASSERT_FALSE(r.ok);
    ASSERT_CONTAINS(r.error, "unknown");
}

TEST("calc: error on unbalanced paren") {
    auto r = evalExpr("(2+3");
    ASSERT_FALSE(r.ok);
    ASSERT_CONTAINS(r.error, "')'");
}

TEST("calc: error on trailing junk") {
    auto r = evalExpr("2 3");
    ASSERT_FALSE(r.ok);
    ASSERT_CONTAINS(r.error, "trailing");
}

TEST("calc: format integer-valued without decimals") {
    ASSERT_EQ(formatCalc(5.0), std::string("5"));
    ASSERT_EQ(formatCalc(4294967296.0), std::string("4294967296"));
}

TEST("calc: format fractional trims zeros") {
    ASSERT_EQ(formatCalc(2.5), std::string("2.5"));
}

// --- adversarial review fixes (2026-06-28) ---

TEST("calc: unary minus binds looser than power (-2**2 == -4)") {
    auto r = evalExpr("-2**2");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, -4.0);   // Python/math convention, NOT (-2)**2
}

TEST("calc: paren forces (-2)**2 == 4") {
    auto r = evalExpr("(-2)**2");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 4.0);
}

TEST("calc: negative exponent still works (2**-1)") {
    auto r = evalExpr("2**-1");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 0.5);
}

TEST("calc: power stays right-associative after refactor") {
    auto r = evalExpr("2**3**2");   // 2**(3**2) = 512
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.value, 512.0);
}

TEST("calc: malformed double-dot number is rejected") {
    auto r = evalExpr("1..2");
    ASSERT_FALSE(r.ok);
    ASSERT_CONTAINS(r.error, "number");
}

TEST("calc: trailing-e number is rejected") {
    auto r = evalExpr("1e");
    ASSERT_FALSE(r.ok);
}
