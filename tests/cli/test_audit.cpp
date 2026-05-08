// Phase 32 T3 — wiki audit metric aggregation queries.
// Verifies SQL shape used by audit command against synthetic data.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"

TEST("audit: count by kind") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE graph_nodes("
           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " path TEXT NOT NULL UNIQUE,"
           " kind TEXT NOT NULL DEFAULT 'file',"
           " parent_id INTEGER, symbol_name TEXT)");
    db.run("INSERT INTO graph_nodes(path, kind) VALUES('a', 'file')");
    db.run("INSERT INTO graph_nodes(path, kind) VALUES('b', 'file')");
    db.run("INSERT INTO graph_nodes(path, kind, symbol_name) VALUES('a:x', 'sp', 'sp_x')");
    db.run("INSERT INTO graph_nodes(path, kind, symbol_name) VALUES('a:y', 'table', 'Customer')");

    int n_files = 0, n_sps = 0, n_tables = 0;
    db.query("SELECT COUNT(*) FROM graph_nodes WHERE kind='file'", {},
             [&](const icmg::core::Row& r){ if (!r.empty()) n_files = std::stoi(r[0]); });
    db.query("SELECT COUNT(*) FROM graph_nodes WHERE kind='sp'", {},
             [&](const icmg::core::Row& r){ if (!r.empty()) n_sps = std::stoi(r[0]); });
    db.query("SELECT COUNT(*) FROM graph_nodes WHERE kind='table'", {},
             [&](const icmg::core::Row& r){ if (!r.empty()) n_tables = std::stoi(r[0]); });
    ASSERT_EQ(n_files, 2);
    ASSERT_EQ(n_sps, 1);
    ASSERT_EQ(n_tables, 1);
}

TEST("audit: importance distribution") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE memory_nodes("
           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " topic TEXT NOT NULL, content TEXT NOT NULL,"
           " importance INTEGER NOT NULL DEFAULT 1,"
           " deleted_at INTEGER)");
    db.run("INSERT INTO memory_nodes(topic,content,importance) VALUES('a','x',3)");
    db.run("INSERT INTO memory_nodes(topic,content,importance) VALUES('b','x',2)");
    db.run("INSERT INTO memory_nodes(topic,content,importance) VALUES('c','x',2)");
    db.run("INSERT INTO memory_nodes(topic,content,importance,deleted_at) VALUES('d','x',1,123)");

    int imp1=0, imp2=0, imp3=0;
    db.query("SELECT importance, COUNT(*) FROM memory_nodes "
             "WHERE deleted_at IS NULL GROUP BY importance", {},
             [&](const icmg::core::Row& r){
                 if (r.size() < 2) return;
                 int imp = std::stoi(r[0]); int c = std::stoi(r[1]);
                 if (imp == 1) imp1 = c; else if (imp == 2) imp2 = c;
                 else if (imp == 3) imp3 = c;
             });
    ASSERT_EQ(imp3, 1);
    ASSERT_EQ(imp2, 2);
    ASSERT_EQ(imp1, 0);
}

TEST("audit: verifications pass/fail in window") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE verifications("
           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " command TEXT NOT NULL, exit_code INTEGER NOT NULL DEFAULT 0,"
           " output_head TEXT, recorded_at INTEGER NOT NULL DEFAULT 0)");
    int64_t now_ish = 1000000;
    db.run("INSERT INTO verifications(command, exit_code, recorded_at) VALUES('a', 0, ?)",
           {std::to_string(now_ish)});
    db.run("INSERT INTO verifications(command, exit_code, recorded_at) VALUES('b', 1, ?)",
           {std::to_string(now_ish)});
    db.run("INSERT INTO verifications(command, exit_code, recorded_at) VALUES('c', 1, ?)",
           {std::to_string(now_ish)});

    int p = 0, f = 0;
    db.query("SELECT exit_code FROM verifications WHERE recorded_at > 0", {},
             [&](const icmg::core::Row& r){
                 if (r.empty()) return;
                 int rc = std::stoi(r[0]);
                 if (rc == 0) ++p; else ++f;
             });
    ASSERT_EQ(p, 1);
    ASSERT_EQ(f, 2);
}

int main() {
    std::cout << "=== audit tests ===\n";
    return icmg::test::run_all();
}
