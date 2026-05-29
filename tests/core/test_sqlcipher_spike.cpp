// v1.76 T1 build spike: prove SQLCipher (CNG) gives a keyed DB that round-trips
// and that a WRONG key cannot read. If this passes on MSVC, the encryption
// foundation is sound and we proceed; if it can't build, pivot to OpenSSL.

#include "../test_main.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

static const char* KEY_A =
    "PRAGMA key=\"x'00112233445566778899aabbccddeeff"
    "00112233445566778899aabbccddeeff'\";";
static const char* KEY_B =
    "PRAGMA key=\"x'ffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffff'\";";

TEST("sqlcipher-spike: keyed DB writes + reads back; wrong key fails") {
    auto p = (fs::temp_directory_path() / "icmg_spike.db").string();
    std::error_code ec; fs::remove(p, ec);

    // Create keyed DB, write a row.
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(p.c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, KEY_A, nullptr, nullptr, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "CREATE TABLE t(x);INSERT INTO t VALUES('secret');",
                           nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);

    // Correct key → readable.
    db = nullptr;
    ASSERT_EQ(sqlite3_open(p.c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, KEY_A, nullptr, nullptr, nullptr), SQLITE_OK);
    int good = sqlite3_exec(db, "SELECT count(*) FROM t;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    ASSERT_EQ(good, SQLITE_OK);

    // Wrong key → cannot read (SQLCipher refuses).
    db = nullptr;
    ASSERT_EQ(sqlite3_open(p.c_str(), &db), SQLITE_OK);
    sqlite3_exec(db, KEY_B, nullptr, nullptr, nullptr);
    int bad = sqlite3_exec(db, "SELECT count(*) FROM t;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    ASSERT_FALSE(bad == SQLITE_OK);

    // Plaintext on disk must NOT contain the secret (proves real encryption).
    std::ifstream f(p, std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(f)), {});
    ASSERT_TRUE(raw.find("secret") == std::string::npos);

    fs::remove(p, ec);
}
