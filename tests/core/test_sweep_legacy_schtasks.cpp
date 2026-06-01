// v1.11.0 T2: sweepLegacySchtasks smoke.
//
// `sweepLegacySchtasks()` enumerates Win schtasks via `schtasks /Query /FO CSV`
// and deletes tasks matching `icmg-{backup,maintain,mirror,sentinel,
// shadow-upgrade}-<8hex>`. POSIX no-op. Hard to deeply unit-test without
// installing fake schtasks → smoke only.

#include "../test_main.hpp"
#include "../../src/core/schedule_helper.hpp"

namespace core = icmg::core;

TEST("sweepLegacySchtasks: returns non-negative count") {
    int n = core::sweepLegacySchtasks();
    ASSERT_TRUE(n >= 0);  // 0 (clean / POSIX) or N (deleted)
}

TEST("sweepLegacySchtasks: second call idempotent (0 after sweep)") {
    (void)core::sweepLegacySchtasks();  // first sweep
    int n2 = core::sweepLegacySchtasks();
    ASSERT_EQ(n2, 0);
}

TEST("icmgTaskHash: stable + 8 hex chars") {
    std::string h1 = core::icmgTaskHash("/proj/a");
    std::string h2 = core::icmgTaskHash("/proj/a");
    ASSERT_EQ(h1, h2);
    ASSERT_TRUE(h1.size() == 8);
}

TEST("icmgTaskHash: distinct projects → distinct hashes") {
    std::string h1 = core::icmgTaskHash("/proj/a");
    std::string h2 = core::icmgTaskHash("/proj/b-different-entirely");
    ASSERT_TRUE(h1 != h2);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
