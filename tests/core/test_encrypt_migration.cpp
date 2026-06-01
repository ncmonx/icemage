// v1.76 T5: plaintext -> encrypted migration preserves rows + keeps BM25 working.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <cstdlib>

namespace icmg { namespace cli {
    bool migrateToEncrypted(const std::string& path, const std::string& hexkey);
    bool migrateToPlaintext(const std::string& path, const std::string& hexkey);
}}

static const std::string KEY(64, 'a');  // valid 64-hex key

TEST("encrypt-migration: plaintext->encrypted preserves rows + BM25 still finds") {
    namespace fs = std::filesystem;
    auto p = (fs::temp_directory_path() / "icmg_mig.db").string();
    std::error_code ec; fs::remove(p, ec); fs::remove(p + ".pre-encrypt.bak", ec);

    { icmg::core::Db db(p);
      db.run("CREATE VIRTUAL TABLE notes USING fts5(body)");
      db.run("INSERT INTO notes(body) VALUES(?)", {"alpha bravo charlie"});
    }
    ASSERT_TRUE(icmg::cli::migrateToEncrypted(p, KEY));
    // Security: the plaintext .bak must be shredded after a successful swap
    // (leaving it would defeat encryption).
    ASSERT_FALSE(fs::exists(p + ".pre-encrypt.bak"));

    sqlite3* db = nullptr; sqlite3_open(p.c_str(), &db);
    sqlite3_exec(db, ("PRAGMA key=\"x'" + KEY + "'\";").c_str(), nullptr, nullptr, nullptr);
    int hits = 0;
    sqlite3_exec(db, "SELECT count(*) FROM notes WHERE notes MATCH 'bravo'",
        [](void* a,int,char** v,char**){ *(int*)a = (v && v[0]) ? atoi(v[0]) : 0; return 0; },
        &hits, nullptr);
    sqlite3_close(db);
    ASSERT_EQ(hits, 1);

    fs::remove(p, ec); fs::remove(p + ".pre-encrypt.bak", ec);
}

TEST("encrypt-wrongkey: encrypted DB cannot be read with the wrong key") {
    namespace fs = std::filesystem;
    auto p = (fs::temp_directory_path() / "icmg_wk.db").string();
    std::error_code ec; fs::remove(p, ec); fs::remove(p + ".pre-encrypt.bak", ec);

    { icmg::core::Db db(p);
      db.run("CREATE TABLE t(x)");
      db.run("INSERT INTO t VALUES(?)", {"s"});
    }
    ASSERT_TRUE(icmg::cli::migrateToEncrypted(p, KEY));

    sqlite3* db = nullptr; sqlite3_open(p.c_str(), &db);
    sqlite3_exec(db, ("PRAGMA key=\"x'" + std::string(64,'b') + "'\";").c_str(),
                 nullptr, nullptr, nullptr);
    int rc = sqlite3_exec(db, "SELECT count(*) FROM t;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    ASSERT_FALSE(rc == SQLITE_OK);   // wrong key rejected

    fs::remove(p, ec); fs::remove(p + ".pre-encrypt.bak", ec);
}
