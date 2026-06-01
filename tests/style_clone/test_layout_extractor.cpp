// v1.22.0 (SC3 TDD): layout extractor unit tests.

#include "../test_main.hpp"
#include "../../src/style_clone/layout_extractor.hpp"

using icmg::style_clone::extractLayout;
using icmg::style_clone::structuralHash;
using icmg::style_clone::detectLang;
using icmg::style_clone::layoutToJson;
using icmg::style_clone::layoutFromJson;

TEST("layout_extractor: detectLang by extension") {
    ASSERT_EQ(detectLang("a.vue"),    std::string("vue"));
    ASSERT_EQ(detectLang("a.jsx"),    std::string("jsx"));
    ASSERT_EQ(detectLang("a.tsx"),    std::string("tsx"));
    ASSERT_EQ(detectLang("a.html"),   std::string("html"));
    ASSERT_EQ(detectLang("a.svelte"), std::string("svelte"));
    ASSERT_EQ(detectLang("a.unknown"), std::string(""));
}

TEST("layout_extractor: empty source → zero nodes") {
    auto t = extractLayout("", "html");
    ASSERT_EQ(t.node_count, 0);
}

TEST("layout_extractor: simple HTML tag with class") {
    std::string src = R"(<div class="card primary"><span class="title">Hello</span></div>)";
    auto t = extractLayout(src, "html");
    ASSERT_EQ(t.node_count, 2);
    ASSERT_TRUE(t.class_set.count("card") == 1);
    ASSERT_TRUE(t.class_set.count("primary") == 1);
    ASSERT_TRUE(t.class_set.count("title") == 1);
}

TEST("layout_extractor: structural hash stable across whitespace") {
    std::string a = R"(<div class="card"><p class="t">x</p></div>)";
    std::string b = R"(<div   class="card" >
        <p class="t">y</p>
    </div>)";
    auto ta = extractLayout(a, "html");
    auto tb = extractLayout(b, "html");
    ASSERT_EQ(structuralHash(ta), structuralHash(tb));
}

TEST("layout_extractor: structural hash differs on class change") {
    std::string a = R"(<div class="card"><p class="t">x</p></div>)";
    std::string b = R"(<div class="card"><p class="DIFFERENT">x</p></div>)";
    auto ta = extractLayout(a, "html");
    auto tb = extractLayout(b, "html");
    ASSERT_TRUE(structuralHash(ta) != structuralHash(tb));
}

TEST("layout_extractor: Vue SFC isolates <template> block") {
    std::string src =
        "<template>\n"
        "  <div class=\"a\">x</div>\n"
        "</template>\n"
        "<script>const X = '<div class=\"b\">decoy</div>';</script>\n"
        "<style>.c { color: red; }</style>\n";
    auto t = extractLayout(src, "vue");
    // Should ONLY see the <div class=\"a\">, not the decoy in script.
    ASSERT_TRUE(t.class_set.count("a") == 1);
    ASSERT_TRUE(t.class_set.count("b") == 0);
}

TEST("layout_extractor: JSX detects data binding") {
    std::string src = R"(<Button onClick={handle} className="btn">Save</Button>)";
    auto t = extractLayout(src, "jsx");
    ASSERT_EQ(t.node_count, 1);
    ASSERT_TRUE(t.class_set.count("btn") == 1);
    // First child of synthetic root should be the Button with has_data_binding.
    ASSERT_EQ(t.root.children.size(), (size_t)1);
    ASSERT_TRUE(t.root.children[0].has_data_binding);
}

TEST("layout_extractor: JSON round-trip preserves structure") {
    std::string src = R"(<div class="x"><span class="y">z</span></div>)";
    auto t1 = extractLayout(src, "html");
    auto j  = layoutToJson(t1);
    auto t2 = layoutFromJson(j);
    ASSERT_EQ(structuralHash(t1), structuralHash(t2));
    ASSERT_EQ(t1.node_count, t2.node_count);
}

TEST("layout_extractor: self-closing tags don't push onto stack") {
    std::string src = R"(<div class="wrap"><img class="thumb"/><p class="cap">x</p></div>)";
    auto t = extractLayout(src, "html");
    ASSERT_EQ(t.node_count, 3);
    // Both img + p should be siblings under div, not p nested inside img.
    ASSERT_TRUE(t.root.children.size() == 1);  // single div
    ASSERT_TRUE(t.root.children[0].children.size() == 2);  // img + p siblings
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
