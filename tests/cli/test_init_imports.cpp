// Issue #279: `icmg update --apply` (-> `init --install-hooks --force`)
// silently ran `claudemd import --slim`, rewriting a team-shared CLAUDE.md.
// The background import chain must never slim unless explicitly asked, and
// update's hook refresh must skip the imports entirely.
#include "../test_main.hpp"
#include "../../src/cli/init_imports.hpp"
#include <string>
#include <vector>

using namespace icmg::cli;

TEST("init_imports: default chain imports CLAUDE.md WITHOUT --slim") {
    auto c = initImportChain(false, " & ");
    ASSERT_CONTAINS(c, "icmg claudemd import");
    ASSERT_TRUE(c.find("--slim") == std::string::npos);
    ASSERT_CONTAINS(c, "icmg plan import");
    ASSERT_CONTAINS(c, "icmg skill index");
}

TEST("init_imports: explicit opt-in adds --slim to claudemd only") {
    auto c = initImportChain(true, "; ");
    ASSERT_CONTAINS(c, "icmg claudemd import --slim; ");
    ASSERT_CONTAINS(c, "icmg plan import; ");
    ASSERT_TRUE(c.find("plan import --slim") == std::string::npos);
}

TEST("init_imports: --no-import skips, --slim opts in") {
    ASSERT_TRUE(initImportsEnabled({"--force"}));
    ASSERT_FALSE(initImportsEnabled({"--install-hooks", "--no-import"}));
    ASSERT_FALSE(initSlimRequested({"--force"}));
    ASSERT_TRUE(initSlimRequested({"--slim"}));
}

TEST("init_imports: update hook-refresh flags never import") {
    // The exact flag string update_cmd appends to `init` must disable imports.
    std::string f = kUpdateInitRefreshFlags;
    ASSERT_CONTAINS(f, "--install-hooks");
    ASSERT_CONTAINS(f, "--no-import");
    ASSERT_TRUE(f.find("--slim") == std::string::npos);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
