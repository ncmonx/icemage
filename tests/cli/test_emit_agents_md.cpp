// TDD (2026-07-05): emit-agents-md merge semantics.
// Feature #2 from docs/plans/2026-07-04-feature-research-2026-landscape.md
// (DO FIRST). Pure-function layer: sync an icmg-managed routing block into
// AGENTS.md idempotently, preserving hand-written user content.

#include "../test_main.hpp"
#include "../../src/cli/emit_agents_md.hpp"

#include <string>

using icmg::cli::syncAgentsMd;
using icmg::cli::wrapIcmgBlock;
using icmg::cli::icmgRoutingBlock;
using icmg::cli::kAgentsMdStart;
using icmg::cli::kAgentsMdEnd;

// 1. Empty input -> emits both markers wrapping the body.
TEST("emit-agents-md: empty input gets the managed block with markers") {
    std::string out = syncAgentsMd("", "BODY");
    ASSERT_CONTAINS(out, kAgentsMdStart);
    ASSERT_CONTAINS(out, kAgentsMdEnd);
    ASSERT_CONTAINS(out, "BODY");
}

// 2. Idempotent: re-emitting the same body yields identical output.
TEST("emit-agents-md: sync is idempotent") {
    std::string once  = syncAgentsMd("", "BODY");
    std::string twice = syncAgentsMd(once, "BODY");
    ASSERT_EQ(once, twice);
}

// 3. User content ABOVE the block is preserved on re-emit.
TEST("emit-agents-md: preserves hand-written content above the block") {
    std::string user = "# My Project\n\nHand-written rules here.\n";
    std::string out  = syncAgentsMd(user, "BODY");
    ASSERT_CONTAINS(out, "Hand-written rules here.");
    ASSERT_CONTAINS(out, "BODY");
}

// 4. Replace: an updated body replaces the OLD block content, not append twice.
TEST("emit-agents-md: re-emit replaces old block body, no duplication") {
    std::string first  = syncAgentsMd("# Head\n", "OLD_BODY");
    std::string second = syncAgentsMd(first, "NEW_BODY");
    ASSERT_CONTAINS(second, "NEW_BODY");
    ASSERT_NOT_CONTAINS(second, "OLD_BODY");
    // exactly one managed block (start marker appears once)
    auto p = second.find(kAgentsMdStart);
    auto q = second.find(kAgentsMdStart, p + 1);
    ASSERT_TRUE(q == std::string::npos);
    // user heading survives
    ASSERT_CONTAINS(second, "# Head");
}

// 5. The default routing block advertises icmg-first primitives.
TEST("emit-agents-md: default routing block lists icmg primitives") {
    std::string b = icmgRoutingBlock();
    ASSERT_CONTAINS(b, "icmg context");
    ASSERT_CONTAINS(b, "icmg graph symbol");
    ASSERT_CONTAINS(b, "icmg parallel");
}
