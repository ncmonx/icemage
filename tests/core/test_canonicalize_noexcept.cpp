// v1.78 (#174): canonicalize()/isWithinRoot() must never throw. On some Windows
// Server SKUs std::filesystem::canonical reaches the PathCch API-set
// (api-ms-win-core-path) and raises filesystem_error err126 ("specified module
// could not be found") — which crashed `icmg context`. These must use the
// error_code overloads + a purely-lexical fallback instead of throwing.
#include "../test_main.hpp"
#include "../../src/core/path_utils.hpp"
#include <string>

using icmg::core::canonicalize;
using icmg::core::isWithinRoot;

TEST("canonicalize: non-existent path (weak) returns non-empty, no throw") {
    std::string out;
    bool threw = false;
    try { out = canonicalize("some/relative/does-not-exist.txt", /*require_exists*/false); }
    catch (...) { threw = true; }
    ASSERT_FALSE(threw);
    ASSERT_TRUE(!out.empty());
}

TEST("canonicalize: empty + odd input never throws") {
    bool threw = false;
    try { (void)canonicalize("", false); (void)canonicalize("::::", false); }
    catch (...) { threw = true; }
    ASSERT_FALSE(threw);
}

TEST("canonicalize: require_exists on a missing path does not throw") {
    bool threw = false;
    try { (void)canonicalize("/no/such/path/xyz123", /*require_exists*/true); }
    catch (...) { threw = true; }
    ASSERT_FALSE(threw);
}

TEST("isWithinRoot: basic containment still works + no throw") {
    bool threw = false;
    bool inside = false, outside = true;
    try {
        inside  = isWithinRoot("/root/sub/file.txt", "/root");
        outside = isWithinRoot("/other/file.txt", "/root");
    } catch (...) { threw = true; }
    ASSERT_FALSE(threw);
    ASSERT_TRUE(inside);
    ASSERT_FALSE(outside);
}
