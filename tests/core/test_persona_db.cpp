// v1.58 FU1: persona_db unit tests (TDD gap from v1.57 S2).
//
// Exercises readPersona/writePersona/personaDbAvailable against the
// exe-dir persona DB. Uses a unique user_id per run so it never collides
// with a real persona row.

#include "../test_main.hpp"
#include "../../src/core/persona_db.hpp"

#include <string>

using namespace icmg::core;

namespace {
// Unique-ish test user id to avoid clobbering real personas.
const std::string kU = "__test_persona_db_user__";
}

TEST("persona_db: write then read round-trips") {
    bool ok = writePersona(kU, "TestPersona", "warm, terse");
    ASSERT_TRUE(ok);
    std::string p, t;
    bool found = readPersona(kU, p, t);
    ASSERT_TRUE(found);
    ASSERT_EQ(p, std::string("TestPersona"));
    ASSERT_EQ(t, std::string("warm, terse"));
}

TEST("persona_db: overwrite updates existing row") {
    writePersona(kU, "First", "a");
    writePersona(kU, "Second", "b");
    std::string p, t;
    ASSERT_TRUE(readPersona(kU, p, t));
    ASSERT_EQ(p, std::string("Second"));
    ASSERT_EQ(t, std::string("b"));
}

TEST("persona_db: read unknown user returns false") {
    std::string p, t;
    bool found = readPersona("__no_such_user_xyz__", p, t);
    ASSERT_FALSE(found);
}

TEST("persona_db: empty traits round-trip") {
    writePersona(kU, "NoTraits", "");
    std::string p, t;
    ASSERT_TRUE(readPersona(kU, p, t));
    ASSERT_EQ(p, std::string("NoTraits"));
    ASSERT_EQ(t, std::string(""));
}

TEST("persona_db: personaDbAvailable does not throw") {
    // In the test harness the exe dir is writable, so this should be true;
    // but the contract is only that it never throws.
    bool avail = personaDbAvailable();
    (void)avail;
    ASSERT_TRUE(true);
}
