// 2026-06-06: auto-consolidate decision helpers (feature #6). Pure threshold+cooldown math.
#include "../test_main.hpp"
#include "../../src/imem/auto_consolidate.hpp"

#include <filesystem>
#include <fstream>

using namespace icmg::imem;

TEST("auto_consolidate: below threshold -> no act") {
    ASSERT_FALSE(shouldAutoConsolidate(999, 1000, /*last*/0, /*now*/1000000, /*cd*/86400));
    ASSERT_FALSE(shouldShowHint(999, 1000, 0, 1000000, 86400));
}

TEST("auto_consolidate: at threshold + cooldown elapsed -> act") {
    ASSERT_TRUE(shouldAutoConsolidate(1000, 1000, 0, 1000000, 86400));
    ASSERT_TRUE(shouldShowHint(1000, 1000, 0, 1000000, 86400));
}

TEST("auto_consolidate: above threshold but within cooldown -> no act") {
    long long now = 1000000, last = now - 100; // 100s ago, cd 86400
    ASSERT_FALSE(shouldAutoConsolidate(5000, 1000, last, now, 86400));
    ASSERT_FALSE(shouldShowHint(5000, 1000, last, now, 86400));
}

TEST("auto_consolidate: threshold <= 0 disables") {
    ASSERT_FALSE(shouldAutoConsolidate(99999, 0, 0, 1000000, 86400));
    ASSERT_FALSE(shouldShowHint(99999, 0, 0, 1000000, 86400));
}

TEST("auto_consolidate: zoneMarkerName sanitizes unsafe chars") {
    ASSERT_EQ(zoneMarkerName("default"), std::string("consolidate-default.ts"));
    ASSERT_EQ(zoneMarkerName("a/b c.d"), std::string("consolidate-a_b_c_d.ts"));
    ASSERT_EQ(zoneMarkerName("_keep"),   std::string("consolidate-_keep.ts"));
}

TEST("auto_consolidate: marker round-trip + missing/corrupt -> 0") {
    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path() / "icmg_consolidate_marker_test.ts";
    std::error_code ec; fs::remove(p, ec);
    ASSERT_EQ(readMarkerTs(p.string()), 0LL);          // missing -> 0
    writeMarkerTs(p.string(), 1730000000LL);
    ASSERT_EQ(readMarkerTs(p.string()), 1730000000LL); // round-trip
    { std::ofstream f(p); f << "not-a-number"; }
    ASSERT_EQ(readMarkerTs(p.string()), 0LL);          // corrupt -> 0
    fs::remove(p, ec);
}
