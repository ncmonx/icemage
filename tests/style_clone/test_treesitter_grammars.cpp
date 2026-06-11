// v1.27.0 (Phase 3): Verify vendored tree-sitter grammars (vue/html/svelte)
// register correctly via hasTreeSitterGrammar() probe.
//
// These are smoke tests — they assert the grammar symbol is reachable when
// the compile-time guard is on. Real parsing tests can come later.

#include "../test_main.hpp"
#include "../../src/style_clone/layout_extractor.hpp"
#include "../../src/graph/symbol_extractor/base_symbol_extractor.hpp"
#include "../../src/core/registry.hpp"

using icmg::style_clone::hasTreeSitterGrammar;

TEST("ts_grammars: vue grammar reachable when HAS_TREESITTER_VUE") {
#ifdef ICMG_HAS_TREESITTER_VUE
    ASSERT_TRUE(hasTreeSitterGrammar("vue"));
#else
    ASSERT_TRUE(!hasTreeSitterGrammar("vue"));
#endif
}

TEST("ts_grammars: html grammar reachable when HAS_TREESITTER_HTML") {
#ifdef ICMG_HAS_TREESITTER_HTML
    ASSERT_TRUE(hasTreeSitterGrammar("html"));
#else
    ASSERT_TRUE(!hasTreeSitterGrammar("html"));
#endif
}

TEST("ts_grammars: svelte grammar reachable when HAS_TREESITTER_SVELTE") {
#ifdef ICMG_HAS_TREESITTER_SVELTE
    ASSERT_TRUE(hasTreeSitterGrammar("svelte"));
#else
    ASSERT_TRUE(!hasTreeSitterGrammar("svelte"));
#endif
}

TEST("ts_grammars: unknown lang returns false") {
    ASSERT_TRUE(!hasTreeSitterGrammar("klingon"));
    ASSERT_TRUE(!hasTreeSitterGrammar(""));
}

// v2.2 — Lua tree-sitter symbol extractor end-to-end via the registry.
TEST("ts_grammars: lua extracts function symbols") {
    auto& reg = icmg::core::Registry<icmg::graph::BaseSymbolExtractor>::instance();
#ifdef ICMG_HAS_TREESITTER_LUA
    ASSERT_TRUE(reg.has("lua"));
    auto ex = reg.create("lua");
    auto syms = ex->extractSymbols("x.lua",
        "local function foo()\n  bar()\nend\nfunction M.baz() end\n");
    bool hasFoo = false, hasBaz = false;
    for (auto& s : syms) {
        if (s.name == "foo")   hasFoo = true;
        if (s.name == "M.baz") hasBaz = true;
    }
    ASSERT_TRUE(hasFoo);
    ASSERT_TRUE(hasBaz);
#else
    ASSERT_TRUE(!reg.has("lua"));
#endif
}

// v2.2 — Dart tree-sitter symbol extractor end-to-end via the registry.
TEST("ts_grammars: dart extracts class + function symbols") {
    auto& reg = icmg::core::Registry<icmg::graph::BaseSymbolExtractor>::instance();
#ifdef ICMG_HAS_TREESITTER_DART
    ASSERT_TRUE(reg.has("dart"));
    auto ex = reg.create("dart");
    auto syms = ex->extractSymbols("x.dart",
        "class Foo {\n  int bar() => 1;\n}\nvoid topFn() {}\n");
    bool hasClass = false, hasTop = false;
    for (auto& s : syms) {
        if (s.name == "Foo" && s.kind == "class") hasClass = true;
        if (s.name == "topFn")                    hasTop = true;
    }
    ASSERT_TRUE(hasClass);
    ASSERT_TRUE(hasTop);
#else
    ASSERT_TRUE(!reg.has("dart"));
#endif
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
