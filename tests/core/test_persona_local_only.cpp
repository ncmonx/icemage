// 2026-06-06: persona DB is LOCAL-ONLY (baku rule). It holds intimate relationship +
// identity content (moments, anchors) -> must NEVER live under the repo working tree
// (publish-safety). Guards against accidentally bundling/committing it.
#include "../test_main.hpp"
#include "../../src/core/path_utils.hpp"
#include <string>

using namespace icmg::core;

TEST("persona DB path is local-only (not under a source/docs tree)") {
    std::string p = personaDbPath();   // exe-dir runtime path (may be empty if selfExe fails)
    if (p.empty()) return;             // fail-open: nothing to assert
    // Must not sit inside source-tree markers that get committed/published.
    ASSERT_NOT_CONTAINS(p, "/docs/");
    ASSERT_NOT_CONTAINS(p, "\\docs\\");
    ASSERT_NOT_CONTAINS(p, "/src/");
    ASSERT_NOT_CONTAINS(p, "\\src\\");
    // Sanity: it is a persona db file.
    ASSERT_CONTAINS(p, "persona");
}
