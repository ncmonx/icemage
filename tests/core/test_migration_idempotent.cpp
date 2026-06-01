// v1.67: migration idempotency under re-run / concurrent open.
// Regression for the "duplicate column" race: a second migrate pass on an
// already-migrated DB (or a process that opens the same file again) must be
// a clean no-op, never re-run ADD COLUMN.

#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"

#include <filesystem>
#include <string>

using namespace icmg::core;

namespace {
std::string tmpdb(const std::string& name) {
    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path() / name;
    std::error_code ec; fs::remove(p, ec);
    return p.string();
}
}

TEST("migrate: embedded path runs to head version on fresh file") {
    std::string path = tmpdb("icmg_mig_fresh.db");
    int v = 0;
    { Db db(path); Migrator mig("/no/such/migrations/dir"); mig.runAll(db); v = db.userVersion(); }
    ASSERT_TRUE(v > 0);                        // some migrations applied
    std::error_code ec; std::filesystem::remove(path, ec);
}

TEST("migrate: second runAll on same connection is a clean no-op") {
    std::string path = tmpdb("icmg_mig_rerun.db");
    int v1 = 0; bool threw = false; int v2 = -1;
    {
        Db db(path);
        Migrator mig("/no/such/migrations/dir");
        mig.runAll(db);
        v1 = db.userVersion();
        try { mig.runAll(db); }                // re-run: must NOT dup-column
        catch (...) { threw = true; }
        v2 = db.userVersion();
    }
    ASSERT_FALSE(threw);
    ASSERT_EQ(v2, v1);
    std::error_code ec; std::filesystem::remove(path, ec);
}

TEST("migrate: reopening same file re-migrates cleanly (simulates 2nd process)") {
    std::string path = tmpdb("icmg_mig_reopen.db");
    int v1 = 0;
    { Db db1(path); Migrator m("/no/such/dir"); m.runAll(db1); v1 = db1.userVersion(); }
    // Second handle on the same file — the case that raced before.
    bool threw = false; int v2 = -1;
    try {
        Db db2(path);
        Migrator m("/no/such/dir");
        m.runAll(db2);
        v2 = db2.userVersion();
    } catch (...) { threw = true; }
    ASSERT_FALSE(threw);
    ASSERT_EQ(v2, v1);
    std::filesystem::remove(path);
}
