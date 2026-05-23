// Phase 30 T4 — known-issue auto fingerprint clustering logic.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include <map>
#include <string>
#include <cctype>

static std::string fingerprint(const std::string& head) {
    std::string fp;
    for (char c : head) {
        if (std::isalnum((unsigned char)c)) fp.push_back(c);
        if (fp.size() >= 80) break;
    }
    return fp;
}

TEST("known-issue auto: same stderr -> same fingerprint") {
    auto fp1 = fingerprint("error: cannot find module 'foo'");
    auto fp2 = fingerprint("error: cannot find module 'foo'");
    ASSERT_EQ(fp1, fp2);
}

TEST("known-issue auto: different stderr -> different fingerprint") {
    auto a = fingerprint("error: cannot find module 'foo'");
    auto b = fingerprint("error: TypeError: undefined is not a function");
    ASSERT_TRUE(a != b);
}

TEST("known-issue auto: fingerprint length capped at 80") {
    std::string long_err(500, 'X');
    auto fp = fingerprint(long_err);
    ASSERT_EQ((int)fp.size(), 80);
}

TEST("known-issue auto: cluster grouping by fingerprint") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE verifications("
           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " phase INTEGER, command TEXT NOT NULL,"
           " exit_code INTEGER NOT NULL DEFAULT 0,"
           " output_hash TEXT, output_head TEXT,"
           " duration_ms INTEGER,"
           " recorded_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
    // 3 failures with same head -> 1 cluster of 3.
    for (int i = 0; i < 3; ++i) {
        db.run("INSERT INTO verifications(command, exit_code, output_head, recorded_at)"
               " VALUES('cargo build', 1, 'error: missing dep crate-x', strftime('%s','now'))");
    }
    db.run("INSERT INTO verifications(command, exit_code, output_head, recorded_at)"
           " VALUES('npm test', 1, 'TypeError: undefined', strftime('%s','now'))");

    std::map<std::string, int> clusters;
    db.query("SELECT output_head FROM verifications WHERE exit_code != 0", {},
             [&](const icmg::core::Row& r){
                 if (r.empty()) return;
                 ++clusters[fingerprint(r[0])];
             });
    int dep_count = 0, type_count = 0;
    for (auto& [fp, c] : clusters) {
        if (fp.find("errormissingdep") != std::string::npos) dep_count = c;
        if (fp.find("TypeError") != std::string::npos)        type_count = c;
    }
    ASSERT_EQ(dep_count, 3);
    ASSERT_EQ(type_count, 1);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
