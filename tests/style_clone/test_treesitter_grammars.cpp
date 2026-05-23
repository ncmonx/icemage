// v1.27.0 (Phase 3): Verify vendored tree-sitter grammars (vue/html/svelte)
// register correctly via hasTreeSitterGrammar() probe.
//
// These are smoke tests — they assert the grammar symbol is reachable when
// the compile-time guard is on. Real parsing tests can come later.

#include "../test_main.hpp"
#include "../../src/style_clone/layout_extractor.hpp"

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


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
