// Phase 37 T2: tree-sitter C extractor unit tests.
// Compiled & linked only when ICMG_HAS_TREESITTER is defined.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/symbol_extractor/base_symbol_extractor.hpp"

using icmg::graph::BaseSymbolExtractor;
using icmg::graph::Symbol;
using Reg = icmg::core::Registry<BaseSymbolExtractor>;

#ifdef ICMG_HAS_TREESITTER

TEST("treesitter-c: function + struct + enum + typedef") {
    auto e = Reg::instance().create("ast-c");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "#include <stdio.h>\n"
        "struct Point { int x; int y; };\n"
        "typedef struct Point Pt;\n"
        "enum Color { RED, GREEN, BLUE };\n"
        "int add(int a, int b) { return a + b; }\n"
        "void greet(const char* name) {\n"
        "    printf(\"hi %s\\n\", name);\n"
        "    add(1, 2);\n"
        "}\n";
    auto syms = e->extractSymbols("sample.c", src);

    bool fn_add = false, fn_greet = false, st_point = false,
         en_color = false, td_pt = false;
    bool greet_calls_printf = false, greet_calls_add = false;
    for (auto& s : syms) {
        if (s.kind == "function" && s.name == "add") fn_add = true;
        if (s.kind == "function" && s.name == "greet") {
            fn_greet = true;
            for (auto& c : s.calls) {
                if (c == "printf") greet_calls_printf = true;
                if (c == "add")    greet_calls_add = true;
            }
        }
        if (s.kind == "struct" && s.name == "Point") st_point = true;
        if (s.kind == "enum"   && s.name == "Color") en_color = true;
        if (s.kind == "typedef" && s.name == "Pt")   td_pt = true;
    }
    ASSERT_TRUE(fn_add);
    ASSERT_TRUE(fn_greet);
    ASSERT_TRUE(st_point);
    ASSERT_TRUE(en_color);
    ASSERT_TRUE(td_pt);
    ASSERT_TRUE(greet_calls_printf);
    ASSERT_TRUE(greet_calls_add);
}

TEST("treesitter-c: empty/garbage input returns empty without crash") {
    auto e = Reg::instance().create("ast-c");
    ASSERT_TRUE(e != nullptr);
    auto a = e->extractSymbols("x.c", "");
    ASSERT_EQ((int)a.size(), 0);
    auto b = e->extractSymbols("x.c", "@@@ not c at all @@@");
    // Parser may produce 0 or some error nodes — must not crash.
    (void)b;
}

#else

TEST("treesitter-c: build skipped (ICMG_HAS_TREESITTER not defined)") {
    // No-op when tree-sitter disabled. Test exists so CMake target stays valid.
    ASSERT_TRUE(true);
}

#endif


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
