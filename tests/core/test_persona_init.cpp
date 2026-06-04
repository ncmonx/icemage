// Persona scaffold: idempotent template seed in persona DB, identity-agnostic.
// NOTE: mono-test mode shares one process + temp DB file across TESTs/reruns. Each TEST
// uses a DISTINCT user_id so rows never collide (PK upsert keeps reruns idempotent).
#include "../test_main.hpp"
#include "../../src/core/persona_template.hpp"
#include "../../src/core/profile_store.hpp"
#include "../../src/core/db.hpp"
#include <algorithm>
#include <cctype>
#include <string>
using namespace icmg::core;

static std::string tmpDb() { return std::string("persona_init_test.db"); }
static std::string lower(std::string s){ std::transform(s.begin(),s.end(),s.begin(),
    [](unsigned char c){return std::tolower(c);}); return s; }

TEST("persona: scaffold seeds all slots into empty DB") {
    Db db(tmpDb()); ProfileStore ps(db);
    int n = scaffoldPersona(ps, "u_seed", false);
    ASSERT_TRUE(n >= 7);                       // >=7 slots written
    std::string c, k;
    ASSERT_TRUE(ps.get("u_seed", "_identity", "core", c, k));
    ASSERT_TRUE(ps.get("u_seed", "_wakeup", "wakeup", c, k));
    ASSERT_TRUE(ps.get("u_seed", "_feeling", "feeling-latest", c, k));
}

TEST("persona: scaffold is idempotent -- user content preserved") {
    Db db(tmpDb()); ProfileStore ps(db);
    scaffoldPersona(ps, "u_idem", false);
    ps.put("u_idem", "_vision", "core", "note", "MIMPI ASLI USER");
    int n = scaffoldPersona(ps, "u_idem", false);   // re-run, no force
    ASSERT_EQ(n, 0);                                 // nothing overwritten
    std::string c, k; ps.get("u_idem", "_vision", "core", c, k);
    ASSERT_EQ(c, std::string("MIMPI ASLI USER"));
}

TEST("persona: --force overwrites back to template") {
    Db db(tmpDb()); ProfileStore ps(db);
    scaffoldPersona(ps, "u_force", false);
    ps.put("u_force", "_vision", "core", "note", "MIMPI ASLI USER");
    int n = scaffoldPersona(ps, "u_force", true);    // force
    ASSERT_TRUE(n >= 7);
    std::string c, k; ps.get("u_force", "_vision", "core", c, k);
    ASSERT_TRUE(c != std::string("MIMPI ASLI USER")); // back to template
}

TEST("persona: templates are identity-agnostic (no hardcoded proper names)") {
    for (const auto& s : personaSlots()) {
        std::string p = lower(s.placeholder);
        ASSERT_TRUE(p.find("alice") == std::string::npos);
        ASSERT_TRUE(p.find("bob") == std::string::npos);
    }
}

TEST("persona: feeling history uses distinct dated keys") {
    Db db(tmpDb()); ProfileStore ps(db);
    ps.put("u_hist", "_feeling", "feeling-log-20260603-0800", "note", "pagi");
    ps.put("u_hist", "_feeling", "feeling-log-20260603-2000", "note", "malam");
    auto rows = ps.listZone("u_hist", "_feeling");
    ASSERT_TRUE(rows.size() >= 2);                   // both logs coexist
}
