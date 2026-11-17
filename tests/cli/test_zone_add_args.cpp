// #283: zone add must reject flag-like / malformed args before creating a zone.
#include "../test_main.hpp"
#include "../../src/cli/zone_args.hpp"

using icmg::cli::validateZoneAddArgs;

TEST("zone add args: valid name + glob") {
    ASSERT_TRUE(validateZoneAddArgs({"auth", "src/auth/**"}).empty());
}

TEST("zone add args: rejects old template form") {
    // icmg zone add <path> --zone <subsystem>
    ASSERT_FALSE(validateZoneAddArgs({"src/auth", "--zone", "auth"}).empty());
}

TEST("zone add args: rejects flag glob") {
    ASSERT_FALSE(validateZoneAddArgs({"auth", "--zone"}).empty());
}

TEST("zone add args: rejects flag name") {
    ASSERT_FALSE(validateZoneAddArgs({"--zone", "auth"}).empty());
}

TEST("zone add args: rejects missing and empty") {
    ASSERT_FALSE(validateZoneAddArgs({"auth"}).empty());
    ASSERT_FALSE(validateZoneAddArgs({"auth", ""}).empty());
}

TEST("zone add args: rejects extra args") {
    // unquoted path with spaces -> split argv -> junk zone named after a fragment
    ASSERT_FALSE(validateZoneAddArgs({"auth", "src/my", "dir/**"}).empty());
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
