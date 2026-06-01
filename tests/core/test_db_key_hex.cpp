// v1.90.0 security TDD (CodeQL cpp/sql-injection #178/#179): the SQLCipher key is
// interpolated into a PRAGMA blob literal x'<key>' (PRAGMA can't bind params), so
// the key MUST be strict hex or a crafted value could escape the literal.
// isHexKey is the fail-closed gate enforced inside resolveDbKey.

#include "../test_main.hpp"
#include "../../src/core/db_key.hpp"

#include <string>

using namespace icmg::core;

TEST("isHexKey: valid 64-char hex accepted") {
    ASSERT_TRUE(isHexKey(std::string(64, 'a')));
    ASSERT_TRUE(isHexKey("00ff00FF"));
    ASSERT_TRUE(isHexKey("deadbeef"));
}

TEST("isHexKey: empty rejected") {
    ASSERT_FALSE(isHexKey(""));
}

TEST("isHexKey: odd length rejected") {
    ASSERT_FALSE(isHexKey("abc"));        // 3 chars
    ASSERT_FALSE(isHexKey("a"));
}

TEST("isHexKey: non-hex char rejected") {
    ASSERT_FALSE(isHexKey("zz"));
    ASSERT_FALSE(isHexKey("00gg"));
}

TEST("isHexKey: injection attempt rejected") {
    // A key trying to break out of x'...' must be refused.
    ASSERT_FALSE(isHexKey("aa'; DROP TABLE x;--"));
    ASSERT_FALSE(isHexKey("aa' OR '1'='1"));
    ASSERT_FALSE(isHexKey("00\"00"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
