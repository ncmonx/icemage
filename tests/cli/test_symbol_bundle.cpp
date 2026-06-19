// TDD (2026-06-15): Feature F - `icmg context --symbol <Name>` (file-less).
// Cross-file symbol bundle: definition + callers + callees via the code graph.
// These cover the pure render helper; bundle_cmd.cpp does the graph gather.
// Failing FIRST: symbol_bundle.hpp absent.

#include "../test_main.hpp"
#include "../../src/cli/symbol_bundle.hpp"

#include <string>

using icmg::cli::SymRef;
using icmg::cli::SymbolBundleData;
using icmg::cli::renderSymbolBundle;
using icmg::cli::dedupRefsByName;

// 1. Header shows the symbol name + kind.
TEST("symbol-bundle: header shows name and kind") {
    SymbolBundleData d;
    d.def = {"GraphStore", "class", "src/graph/graph_store.hpp", 14, 160};
    auto s = renderSymbolBundle(d);
    ASSERT_TRUE(s.find("Symbol: GraphStore") != std::string::npos);
    ASSERT_TRUE(s.find("[class]") != std::string::npos);
}

// 2. Definition line shows the path and L<start>-<end>.
TEST("symbol-bundle: def line shows path and line range") {
    SymbolBundleData d;
    d.def = {"foo", "function", "src/a.cpp", 10, 25};
    auto s = renderSymbolBundle(d);
    ASSERT_TRUE(s.find("def: src/a.cpp") != std::string::npos);
    ASSERT_TRUE(s.find("L10-25") != std::string::npos);
}

// 3. Callers are listed with a <- prefix.
TEST("symbol-bundle: callers listed with arrow") {
    SymbolBundleData d;
    d.def = {"foo", "function", "src/a.cpp", 1, 5};
    d.callers.push_back({"bar", "function", "src/b.cpp", 0, 0});
    auto s = renderSymbolBundle(d);
    ASSERT_TRUE(s.find("Callers (1)") != std::string::npos);
    ASSERT_TRUE(s.find("<- bar  (src/b.cpp)") != std::string::npos);
}

// 4. Callees are listed with a -> prefix.
TEST("symbol-bundle: callees listed with arrow") {
    SymbolBundleData d;
    d.def = {"foo", "function", "src/a.cpp", 1, 5};
    d.callees.push_back({"baz", "method", "src/c.cpp", 0, 0});
    auto s = renderSymbolBundle(d);
    ASSERT_TRUE(s.find("Callees (1)") != std::string::npos);
    ASSERT_TRUE(s.find("-> baz  (src/c.cpp)") != std::string::npos);
}

// 5. Empty callers/callees render "(none found)".
TEST("symbol-bundle: empty relations show none found") {
    SymbolBundleData d;
    d.def = {"foo", "function", "src/a.cpp", 1, 5};
    auto s = renderSymbolBundle(d);
    ASSERT_TRUE(s.find("Callers (0)") != std::string::npos);
    ASSERT_TRUE(s.find("Callees (0)") != std::string::npos);
    ASSERT_TRUE(s.find("(none found)") != std::string::npos);
}

// 6. Ambiguity note when more than one definition matched.
TEST("symbol-bundle: ambiguity note when total_matches > 1") {
    SymbolBundleData d;
    d.def = {"foo", "function", "src/a.cpp", 1, 5};
    d.total_matches = 3;
    auto s = renderSymbolBundle(d);
    ASSERT_TRUE(s.find("3 definitions matched") != std::string::npos);
}

// 7. body is embedded under the Definition section.
TEST("symbol-bundle: body embedded under definition") {
    SymbolBundleData d;
    d.def = {"foo", "function", "src/a.cpp", 1, 2};
    d.body = "int foo() { return 1; }";
    auto s = renderSymbolBundle(d);
    ASSERT_TRUE(s.find("--- Definition ---") != std::string::npos);
    ASSERT_TRUE(s.find("int foo() { return 1; }") != std::string::npos);
}

// 8. dedupRefsByName collapses same-name fan-out, keeps first, preserves order.
TEST("symbol-bundle: dedupRefsByName collapses same-name fan-out") {
    std::vector<SymRef> v = {
        {"push_back", "method", "a.hpp", 0, 0},
        {"find",      "method", "a.hpp", 0, 0},
        {"push_back", "method", "b.cpp", 0, 0},
        {"push_back", "method", "c.cpp", 0, 0},
        {"count",     "method", "a.hpp", 0, 0},
    };
    icmg::cli::dedupRefsByName(v);
    ASSERT_EQ((int)v.size(), 3);
    ASSERT_EQ(v[0].name, std::string("push_back"));
    ASSERT_EQ(v[0].path, std::string("a.hpp"));  // first wins
    ASSERT_EQ(v[1].name, std::string("find"));
    ASSERT_EQ(v[2].name, std::string("count"));
}

// 9. dedupRefsByName keys on path when name is empty.
TEST("symbol-bundle: dedupRefsByName keys on path for unnamed refs") {
    std::vector<SymRef> v = {
        {"", "file", "x.cpp", 0, 0},
        {"", "file", "x.cpp", 0, 0},
        {"", "file", "y.cpp", 0, 0},
    };
    icmg::cli::dedupRefsByName(v);
    ASSERT_EQ((int)v.size(), 2);
}
