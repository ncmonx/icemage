// TDD (2026-06-15): serve /api/health endpoint JSON builder.
// Spec: claude-mem worker-health-endpoint idea — a machine-readable status
// probe for the embedded dashboard server (uptime, db reachability, counts).
// Pure builder so the JSON shape is unit-testable without opening a socket.
// Failing FIRST: src/cli/serve_health.hpp does not exist yet.

#include "../test_main.hpp"
#include "../../src/cli/serve_health.hpp"

#include <string>

using icmg::cli::buildHealthJson;

// 1. Reports status=ok, version, uptime, and the supplied counts.
TEST("serve-health: builds ok status JSON with uptime + counts") {
    std::string j = buildHealthJson(/*db_ok*/true, /*uptime_s*/42,
                                    /*mem*/100, /*nodes*/2000, "2.4.2");
    ASSERT_CONTAINS(j, "\"status\":\"ok\"");
    ASSERT_CONTAINS(j, "\"uptime_s\":42");
    ASSERT_CONTAINS(j, "\"memory_nodes\":100");
    ASSERT_CONTAINS(j, "\"graph_nodes\":2000");
    ASSERT_CONTAINS(j, "\"version\":\"2.4.2\"");
}

// 2. db_ok=false flips status to "degraded".
TEST("serve-health: db unreachable reports degraded") {
    std::string j = buildHealthJson(false, 5, 0, 0, "2.4.2");
    ASSERT_CONTAINS(j, "\"status\":\"degraded\"");
    ASSERT_CONTAINS(j, "\"db_ok\":false");
}

// 3. Always valid-ish JSON object (starts { ends }), no trailing comma.
TEST("serve-health: output is a single JSON object") {
    std::string j = buildHealthJson(true, 0, 0, 0, "x");
    ASSERT_TRUE(!j.empty() && j.front() == '{');
    ASSERT_TRUE(j.back() == '}');
    ASSERT_NOT_CONTAINS(j, ",}");
}
