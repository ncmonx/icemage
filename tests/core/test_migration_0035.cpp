// tests/core/test_migration_0035.cpp
//
// Verifies migration 0041: graph_edges gains `confidence` column
// with default value 'EXTRACTED'.
//
// Tests:
//   (1) migration is idempotent (run twice, no error)
//   (2) graph_edges.confidence column exists
//   (3) default value is 'EXTRACTED'

#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST("migration-0035: confidence column exists after migration") {
    fs::path tmp = fs::temp_directory_path() / "test_mig0035.db";
    fs::remove(tmp);
    icmg::core::ensureProjectDb(tmp.string());

    icmg::core::Db db(tmp.string());
    bool found = false;
    db.query("PRAGMA table_info(graph_edges)", {},
        [&](const icmg::core::Row& r) {
            // column name is r[1]
            if (r.size() > 1 && r[1] == "confidence") found = true;
        });
    ASSERT_TRUE(found);
    fs::remove(tmp);
}

TEST("migration-0035: confidence default is EXTRACTED") {
    fs::path tmp = fs::temp_directory_path() / "test_mig0035b.db";
    fs::remove(tmp);
    icmg::core::ensureProjectDb(tmp.string());

    icmg::core::Db db(tmp.string());
    // Insert a minimal edge without specifying confidence
    db.run("INSERT OR IGNORE INTO graph_nodes(path,kind,zone) VALUES('a.cpp','file','default')");
    db.run("INSERT OR IGNORE INTO graph_nodes(path,kind,zone) VALUES('b.cpp','file','default')");
    int64_t src = 0, dst = 0;
    db.query("SELECT id FROM graph_nodes WHERE path='a.cpp'", {},
        [&](const icmg::core::Row& r) { if (!r.empty()) src = std::stoll(r[0]); });
    db.query("SELECT id FROM graph_nodes WHERE path='b.cpp'", {},
        [&](const icmg::core::Row& r) { if (!r.empty()) dst = std::stoll(r[0]); });
    ASSERT_TRUE(src > 0);
    ASSERT_TRUE(dst > 0);

    db.run("INSERT OR REPLACE INTO graph_edges(src,dst,edge_type,weight) VALUES("
           + std::to_string(src) + "," + std::to_string(dst) + ",'imports',1.0)");

    std::string conf;
    db.query("SELECT confidence FROM graph_edges WHERE src=? AND dst=?",
             {std::to_string(src), std::to_string(dst)},
             [&](const icmg::core::Row& r) { if (!r.empty()) conf = r[0]; });
    ASSERT_EQ(conf, std::string("EXTRACTED"));
    fs::remove(tmp);
}

TEST("migration-0035: idempotent (run migrator twice)") {
    fs::path tmp = fs::temp_directory_path() / "test_mig0035c.db";
    fs::remove(tmp);
    icmg::core::ensureProjectDb(tmp.string());
    // Run migrator again — should not throw
    icmg::core::Db db(tmp.string());
    icmg::core::Migrator m;
    bool threw = false;
    try { m.runAll(db); } catch (...) { threw = true; }
    ASSERT_TRUE(!threw);
    fs::remove(tmp);
}
