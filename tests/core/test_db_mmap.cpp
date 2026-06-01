// v1.58 F2: verify memory-mapped hot-read pragma is active on every Db open.

#include "../test_main.hpp"
#include "../../src/core/db.hpp"

#include <filesystem>
#include <string>

using namespace icmg::core;

TEST("F2 mmap: mmap_size pragma is set (>0) on a fresh Db") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "icmg_mmap_test.db";
    std::error_code ec;
    fs::remove(tmp, ec);

    Db db(tmp.string());
    long long mmap_size = -1;
    db.query("PRAGMA mmap_size", {}, [&](const Row& r) {
        if (!r.empty()) mmap_size = std::stoll(r[0]);
    });
    // 256 MB configured in Db ctor; must be > 0 (mmap active).
    ASSERT_TRUE(mmap_size > 0);

    fs::remove(tmp, ec);
}

TEST("F2 mmap: WAL + mmap coexist (journal_mode wal)") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "icmg_mmap_wal_test.db";
    std::error_code ec;
    fs::remove(tmp, ec);

    Db db(tmp.string());
    std::string jmode;
    db.query("PRAGMA journal_mode", {}, [&](const Row& r) {
        if (!r.empty()) jmode = r[0];
    });
    ASSERT_EQ(jmode, std::string("wal"));

    fs::remove(tmp, ec);
}
