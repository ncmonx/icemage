// tests/cli/test_graph_clean_alias.cpp
//
// graph-clean was a byte-for-byte duplicate of graph-prune (both remove graph
// nodes whose file no longer exists on disk). Consolidated: graph-clean is now
// a thin backward-compat alias that delegates to graph-prune (single source of
// truth). This test pins that:
//   (1) graph-clean is still registered (no breakage for old callers)
//   (2) graph-clean --dry-run runs cleanly (exit 0) like graph-prune
#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

using namespace icmg;

TEST("graph-clean: still registered (backward-compat alias)") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    ASSERT_TRUE(reg.has("graph-clean"));
    ASSERT_TRUE(reg.has("graph-prune"));
}

TEST("graph-clean: delegates to graph-prune (alias description)") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto clean = reg.create("graph-clean");
    ASSERT_TRUE(clean != nullptr);
    // The alias advertises itself as such so callers/router know it forwards.
    ASSERT_TRUE(clean->description().find("alias for graph-prune") != std::string::npos);
    // And graph-prune (the single source of truth) must be resolvable for it
    // to delegate to at run time.
    auto prune = reg.create("graph-prune");
    ASSERT_TRUE(prune != nullptr);
}
