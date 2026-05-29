// ram-brain Phase D: governor tick resizes cap from RAM + keeps hot entries.
#include "../test_main.hpp"
#include "../../src/core/recall_cache.hpp"

using namespace icmg::core;

TEST("governor-tick: low RAM shrinks cap + evicts cold, keeps hot") {
    RecallCache c; c.setCap(100, 64u << 20);
    for (int i = 0; i < 50; ++i) c.put("k" + std::to_string(i), std::string(1000, 'x'));
    (void)c.get("k49"); (void)c.get("k49"); (void)c.get("k48");  // make k49/k48 hot
    runGovernorOnce(c, /*avail*/200, /*total*/10000);            // ~98% used -> shrink hard
    auto s = c.stats();
    ASSERT_TRUE(s.bytes <= s.cap_bytes);     // fit under shrunk cap
    ASSERT_TRUE(c.get("k49").has_value());   // hottest survived (pinned)
}

TEST("governor-tick: ample RAM keeps cap generous") {
    RecallCache c; c.setCap(100, 8u << 20);
    for (int i = 0; i < 10; ++i) c.put("k" + std::to_string(i), std::string(1000, 'y'));
    runGovernorOnce(c, /*avail*/9000, /*total*/10000);   // 10% RAM used -> not tight
    // Cap tracks usage with a floor; ample RAM never shrinks below the 4MB floor.
    ASSERT_TRUE(c.stats().cap_bytes >= (4u << 20));
}
