// me-everywhere wire-dir override (ICMG_WIRE_DIR). The wire files
// (presence/bus/lock/msg/hot) default to icmgGlobalDir() but an explicit
// ICMG_WIRE_DIR lets sessions whose %APPDATA% is NOT shared rendezvous on a
// commonly-visible folder. Pure resolver -> unit-testable without env mutation.
#include "../test_main.hpp"
#include "../../src/core/path_utils.hpp"
#include <string>

using namespace icmg::core;

TEST("wire_dir: env set -> returns env value (overrides fallback)") {
    ASSERT_EQ(resolveWireDir("C:/Temp/icmg-wire", "C:/Users/x/AppData/Roaming/icmg"),
              std::string("C:/Temp/icmg-wire"));
}

TEST("wire_dir: env null -> falls back to global dir") {
    ASSERT_EQ(resolveWireDir(nullptr, "/home/u/.icmg"), std::string("/home/u/.icmg"));
}

TEST("wire_dir: env empty string -> falls back (empty != override)") {
    ASSERT_EQ(resolveWireDir("", "/home/u/.icmg"), std::string("/home/u/.icmg"));
}

TEST("wire_dir: env wins even when fallback nonempty + different") {
    std::string got = resolveWireDir("/shared/wire", "/private/icmg");
    ASSERT_EQ(got, std::string("/shared/wire"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
