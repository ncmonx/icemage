// test_ast_compressor — regex-based AST body elision (v1.3 T10).
//
// Implementation lives in src/graph/ast_compressor.cpp. v1.4 plan: swap to
// real tree-sitter S-expr queries. These tests target the v1.3 regex contract.
//
// Note: the C-family loop had an infinite-loop bug when the whole body was on
// the same line as the header (e.g. `int foo() { return 1; }`); the fix is
// `li = bl + 1;` (not `li = bl;`) after consuming a body. Test cases below
// exercise that path explicitly.

#include "../test_main.hpp"
#include "../../src/graph/ast_compressor.hpp"
#include <string>

TEST("compressAst on empty source returns empty") {
    ASSERT_EQ(icmg::graph::compressAst("", "cpp"), std::string(""));
}

TEST("compressAst on unknown lang returns source verbatim") {
    std::string src = "irrelevant content";
    ASSERT_EQ(icmg::graph::compressAst(src, "fortran"), src);
}

TEST("compressAst on single-line cpp fn elides body") {
    std::string src = "int foo() { int x = 1; return x; }";
    std::string out = icmg::graph::compressAst(src, "cpp");
    // Must keep the signature
    ASSERT_CONTAINS(out, "int foo()");
    // Must have the ellipsis placeholder
    ASSERT_CONTAINS(out, "/* ... */");
    // Must NOT contain the implementation detail
    ASSERT_NOT_CONTAINS(out, "int x = 1");
}

TEST("compressAst on multi-line cpp fn elides body") {
    std::string src =
        "int foo() {\n"
        "    int x = 42;\n"
        "    return x;\n"
        "}\n";
    std::string out = icmg::graph::compressAst(src, "cpp");
    ASSERT_CONTAINS(out, "int foo()");
    ASSERT_CONTAINS(out, "/* ... */");
    ASSERT_NOT_CONTAINS(out, "int x = 42");
}

TEST("compressAst on python def elides body") {
    std::string src =
        "def foo():\n"
        "    a = 1\n"
        "    return a\n";
    std::string out = icmg::graph::compressAst(src, "python");
    ASSERT_CONTAINS(out, "def foo():");
    ASSERT_NOT_CONTAINS(out, "a = 1");
}

int main() { return icmg::test::run_all(); }
