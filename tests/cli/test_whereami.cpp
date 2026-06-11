// `icmg whereami` pure-renderer unit tests (2026-06-11).
#include "../test_main.hpp"
#include "../../src/cli/whereami_render.hpp"

using namespace icmg::cli;

TEST("whereami: aligns labels and includes values") {
    auto out = renderWhereAmI({{"binary", "/bin/icmg"}, {"version", "2.3.0"}});
    ASSERT_CONTAINS(out, "binary  : /bin/icmg");   // shorter label padded to 'version'
    ASSERT_CONTAINS(out, "version : 2.3.0");
}

TEST("whereami: empty value -> (unknown)") {
    auto out = renderWhereAmI({{"binary", ""}});
    ASSERT_CONTAINS(out, "binary : (unknown)");
}

TEST("whereami: one line per row") {
    auto out = renderWhereAmI({{"a", "1"}, {"b", "2"}, {"c", "3"}});
    int nl = 0;
    for (char ch : out) if (ch == '\n') ++nl;
    ASSERT_EQ(nl, 3);
}
