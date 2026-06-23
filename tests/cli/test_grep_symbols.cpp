// TDD (2026-06-15): `icmg grep --symbols` — annotate matches with enclosing symbol.
// Spec: after running rg with line numbers, each match is grouped under the
// function/class that encloses it (looked up from the graph), so search results
// read as "fooBar(): L42 matched text" instead of raw path:line:text noise.
// The two pure helpers (parse rg output + render grouped-by-symbol) are unit-
// testable without a DB; the graph lookup happens in the command between them.
// Failing FIRST: src/cli/grep_symbols.hpp does not exist yet.

#include "../test_main.hpp"
#include "../../src/cli/grep_symbols.hpp"

#include <string>

using icmg::cli::parseGrepMatches;
using icmg::cli::renderSymbolGrep;
using icmg::cli::GrepMatch;

// 1. Parse standard `path:line:text` rg output into structured matches.
TEST("grep-symbols: parses path:line:text rows") {
    std::string out =
        "src/foo.cpp:42:    int x = compute();\n"
        "src/foo.cpp:99:    return x;\n";
    auto m = parseGrepMatches(out);
    ASSERT_EQ((int)m.size(), 2);
    ASSERT_EQ(m[0].path, std::string("src/foo.cpp"));
    ASSERT_EQ(m[0].line, 42);
    ASSERT_CONTAINS(m[0].text, "compute()");
    ASSERT_EQ(m[1].line, 99);
}

// 2. Windows drive-letter paths (colon after drive) parse correctly: the split
//    happens at the `:<digits>:` separator, not the drive colon.
TEST("grep-symbols: handles Windows drive-letter path") {
    std::string out = "D:\\proj\\bar.cpp:7:hit here\n";
    auto m = parseGrepMatches(out);
    ASSERT_EQ((int)m.size(), 1);
    ASSERT_EQ(m[0].path, std::string("D:\\proj\\bar.cpp"));
    ASSERT_EQ(m[0].line, 7);
    ASSERT_CONTAINS(m[0].text, "hit here");
}

// 3. Non-match lines (rg context separators, blank) are ignored.
TEST("grep-symbols: ignores lines without :N: separator") {
    std::string out =
        "src/a.cpp:1:real match\n"
        "--\n"
        "\n"
        "Binary file src/x.bin matches\n";
    auto m = parseGrepMatches(out);
    ASSERT_EQ((int)m.size(), 1);
    ASSERT_EQ(m[0].line, 1);
}

// 4. Render groups matches by file then by enclosing symbol; one header per
//    consecutive same-symbol run; symbol name + kind appear.
TEST("grep-symbols: render groups by enclosing symbol") {
    std::vector<GrepMatch> m = {
        {"src/foo.cpp", 42, "int x = compute();", "computeThing", "function"},
        {"src/foo.cpp", 45, "x += 1;",            "computeThing", "function"},
        {"src/foo.cpp", 90, "class body",         "Widget",       "class"},
    };
    std::string r = renderSymbolGrep(m);
    ASSERT_CONTAINS(r, "src/foo.cpp");
    ASSERT_CONTAINS(r, "computeThing");
    ASSERT_CONTAINS(r, "function");
    ASSERT_CONTAINS(r, "Widget");
    ASSERT_CONTAINS(r, "42");
    ASSERT_CONTAINS(r, "90");
    // The shared-symbol header should appear once, not once per match.
    auto first = r.find("computeThing");
    auto second = r.find("computeThing", first + 1);
    ASSERT_TRUE(second == std::string::npos);
}

// 5. Matches with no enclosing symbol are grouped under a "(no symbol)" bucket,
//    not dropped.
TEST("grep-symbols: top-level matches kept under no-symbol bucket") {
    std::vector<GrepMatch> m = {
        {"src/top.cpp", 3, "#include <x>", "", ""},
    };
    std::string r = renderSymbolGrep(m);
    ASSERT_CONTAINS(r, "src/top.cpp");
    ASSERT_CONTAINS(r, "#include");
    ASSERT_CONTAINS(r, "3");
}
