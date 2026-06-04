// Provenance (Lapis 1): profile_entries carries a free-text source (default 'unknown').
// Persona DB has no Migrator -> source column added via guarded ALTER in ProfileStore ctor.
// NOTE: mono-test shares one temp DB file -> distinct user_id per TEST (PK collision-free).
#include "../test_main.hpp"
#include "../../src/core/profile_store.hpp"
#include "../../src/core/db.hpp"
#include <string>
using namespace icmg::core;

static std::string tmpDb() { return std::string("profile_source_test.db"); }

TEST("profile: put with source round-trips via get") {
    Db db(tmpDb()); ProfileStore ps(db);
    ps.put("u_src", "_vision", "core", "note", "MIMPI", "test-user");
    std::string c, k, src;
    ASSERT_TRUE(ps.get("u_src", "_vision", "core", c, k, src));
    ASSERT_EQ(src, std::string("test-user"));
}

TEST("profile: put without source defaults to unknown") {
    Db db(tmpDb()); ProfileStore ps(db);
    ps.put("u_def", "_x", "k", "note", "v");   // source omitted -> default param
    std::string c, k, src;
    ps.get("u_def", "_x", "k", c, k, src);
    ASSERT_EQ(src, std::string("unknown"));
}

TEST("profile: bootstrap ALTER is idempotent across constructs") {
    Db db(tmpDb());
    { ProfileStore a(db); a.put("u_i", "_z", "k", "note", "v", "s"); }
    { ProfileStore b(db);                 // second ctor: guarded ALTER must not throw
      std::string c, k, src; b.get("u_i", "_z", "k", c, k, src);
      ASSERT_EQ(src, std::string("s")); }
}

TEST("profile: listZone populates source") {
    Db db(tmpDb()); ProfileStore ps(db);
    ps.put("u_l", "_z", "k1", "note", "v1", "test-user");
    auto rows = ps.listZone("u_l", "_z");
    ASSERT_TRUE(rows.size() >= 1);
    ASSERT_EQ(rows[0].source, std::string("test-user"));
}
