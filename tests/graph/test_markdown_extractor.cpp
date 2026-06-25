// tests/graph/test_markdown_extractor.cpp
//
// TDD tests for MarkdownExtractor.
//
// Encoding conventions:
//   imports << "links:<target>"      -> edge_type="links"
//   imports << "wikilinks:<target>"  -> edge_type="wikilinks"
//   functions << heading text        -> heading symbols

#include "../test_main.hpp"
#include "../../src/graph/extractor/base_extractor.hpp"
#include "../../src/core/registry.hpp"
#include <string>

static icmg::graph::BaseExtractor* getMdExt() {
    static auto inst = []() -> std::unique_ptr<icmg::graph::BaseExtractor> {
        auto& reg = icmg::core::Registry<icmg::graph::BaseExtractor>::instance();
        if (!reg.has("markdown")) return nullptr;
        return reg.create("markdown");
    }();
    return inst.get();
}

TEST("markdown-extractor: registered") {
    ASSERT_TRUE(getMdExt() != nullptr);
}

TEST("markdown-extractor: H1/H2 headings in functions list") {
    auto* ext = getMdExt();
    ASSERT_TRUE(ext != nullptr);

    std::string src =
        "# My Title\n"
        "some text\n"
        "## Sub Section\n";
    auto r = ext->extract("README.md", src);

    bool h1 = false, h2 = false;
    for (auto& f : r.functions) {
        if (f == "My Title")    h1 = true;
        if (f == "Sub Section") h2 = true;
    }
    ASSERT_TRUE(h1);
    ASSERT_TRUE(h2);
}

TEST("markdown-extractor: [text](target.md) -> links: import") {
    auto* ext = getMdExt();
    ASSERT_TRUE(ext != nullptr);

    std::string src = "See [foo](docs/foo.md) for details.\n";
    auto r = ext->extract("README.md", src);

    bool found = false;
    for (auto& imp : r.imports)
        if (imp == "links:docs/foo.md") found = true;
    ASSERT_TRUE(found);
}

TEST("markdown-extractor: [[wikilink]] -> wikilinks: import") {
    auto* ext = getMdExt();
    ASSERT_TRUE(ext != nullptr);

    std::string src = "See also [[Architecture]] and [[Setup]].\n";
    auto r = ext->extract("README.md", src);

    bool arch = false, setup = false;
    for (auto& imp : r.imports) {
        if (imp == "wikilinks:Architecture") arch  = true;
        if (imp == "wikilinks:Setup")        setup = true;
    }
    ASSERT_TRUE(arch);
    ASSERT_TRUE(setup);
}

TEST("markdown-extractor: handles .rst extension") {
    auto* ext = getMdExt();
    ASSERT_TRUE(ext != nullptr);

    std::string src = "# Title\n[link](other.rst)\n";
    auto r = ext->extract("guide.rst", src);

    bool found = false;
    for (auto& imp : r.imports)
        if (imp == "links:other.rst") found = true;
    ASSERT_TRUE(found);
}

TEST("markdown-extractor: no crash on empty input") {
    auto* ext = getMdExt();
    ASSERT_TRUE(ext != nullptr);
    auto r = ext->extract("empty.md", "");
    (void)r;
}
