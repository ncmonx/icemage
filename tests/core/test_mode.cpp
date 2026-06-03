// Pins the `icmg mode` contract: set/get/clear over the persona-DB ProfileStore
// using the fixed zone "_mode" + key "current" that ModeCommand relies on.
// (ModeCommand is a thin CLI wrapper over ProfileStore; this locks the storage
// contract so a refactor can't silently move the mode banner off the persona zone.)
// NOTE: mono-test mode shares one process + temp DB persists across TESTs; use a
// DISTINCT user_id per TEST so rows never collide.
#include "../test_main.hpp"
#include "../../src/core/profile_store.hpp"
#include "../../src/core/db.hpp"
#include <string>
using namespace icmg::core;

static std::string modeTmpDb() { return std::string("mode_test.db"); }

// The contract constants ModeCommand uses.
static const char* MODE_ZONE = "_mode";
static const char* MODE_KEY  = "current";

TEST("mode: set then get round-trips the banner text") {
    Db db(modeTmpDb());
    ProfileStore ps(db);
    const std::string text = "long-session test: ship-LOCAL, hunt bugs";
    ps.put("u_mode_set", MODE_ZONE, MODE_KEY, "note", text);
    std::string content, kind;
    bool ok = ps.get("u_mode_set", MODE_ZONE, MODE_KEY, content, kind);
    ASSERT_TRUE(ok);
    ASSERT_EQ(content, text);
    ASSERT_EQ(kind, std::string("note"));
}

TEST("mode: set overwrites previous banner (single current mode)") {
    Db db(modeTmpDb());
    ProfileStore ps(db);
    ps.put("u_mode_ovr", MODE_ZONE, MODE_KEY, "note", "first mode");
    ps.put("u_mode_ovr", MODE_ZONE, MODE_KEY, "note", "second mode");
    std::string content, kind;
    bool ok = ps.get("u_mode_ovr", MODE_ZONE, MODE_KEY, content, kind);
    ASSERT_TRUE(ok);
    ASSERT_EQ(content, std::string("second mode"));
}

TEST("mode: clear removes the banner so get returns false") {
    Db db(modeTmpDb());
    ProfileStore ps(db);
    ps.put("u_mode_clr", MODE_ZONE, MODE_KEY, "note", "to be cleared");
    ps.forget("u_mode_clr", MODE_ZONE, MODE_KEY);
    std::string content, kind;
    bool ok = ps.get("u_mode_clr", MODE_ZONE, MODE_KEY, content, kind);
    ASSERT_TRUE(!ok);
}

TEST("mode: get on never-set user returns false (empty banner)") {
    Db db(modeTmpDb());
    ProfileStore ps(db);
    std::string content, kind;
    bool ok = ps.get("u_mode_never", MODE_ZONE, MODE_KEY, content, kind);
    ASSERT_TRUE(!ok);
}
