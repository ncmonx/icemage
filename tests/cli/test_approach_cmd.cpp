// test_approach_cmd — unit tests for `icmg approach` command.
#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <iostream>

using namespace icmg::core;

// Helper: in-memory fully-migrated DB.
static Db makeDb() {
    Db db(":memory:");
    Migrator migrator("__nonexistent_migrations_dir__");
    migrator.runAll(db);
    return db;
}

// Test 1: Registry has "approach" — returns non-null.
TEST("approach_cmd: registered in Registry") {
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("approach");
    ASSERT_TRUE(cmd != nullptr);
}

// Test 2: --help returns rc=0.
TEST("approach_cmd: --help returns 0") {
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("approach");
    ASSERT_TRUE(cmd != nullptr);
    // Redirect stdout to suppress output during test.
    std::streambuf* old = std::cout.rdbuf();
    std::ostringstream sink;
    std::cout.rdbuf(sink.rdbuf());
    int rc = cmd->run({"--help"});
    std::cout.rdbuf(old);
    ASSERT_EQ(rc, 0);
}

// Test 3: record with missing args returns rc=1.
TEST("approach_cmd: record missing args returns rc=1") {
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("approach");
    ASSERT_TRUE(cmd != nullptr);
    // Redirect stderr to suppress output during test.
    std::streambuf* old = std::cerr.rdbuf();
    std::ostringstream sink;
    std::cerr.rdbuf(sink.rdbuf());
    // "record" with only 1 arg (task) but no approach or --outcome.
    int rc = cmd->run({"record", "my-task"});
    std::cerr.rdbuf(old);
    ASSERT_EQ(rc, 1);
}

// Test 4: lookup with no DB / empty results returns rc=0 (fail-soft).
TEST("approach_cmd: lookup no-DB empty returns rc=0") {
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("approach");
    ASSERT_TRUE(cmd != nullptr);
    // Redirect stdout to suppress output during test.
    std::streambuf* old_out = std::cout.rdbuf();
    std::streambuf* old_err = std::cerr.rdbuf();
    std::ostringstream sink;
    std::cout.rdbuf(sink.rdbuf());
    std::cerr.rdbuf(sink.rdbuf());
    // Pass a nonexistent DB path via a subcommand that does fail-soft.
    // The command uses Config::projectDbPath() internally; for lookup the
    // spec says rc=0 on no-DB/empty. We test by calling lookup on the
    // command object — if it opens no DB it should return 0 silently.
    int rc = cmd->run({"lookup", "nonexistent-task-xyz"});
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    ASSERT_EQ(rc, 0);
}

// Test 5: unknown subcommand returns rc=1.
TEST("approach_cmd: unknown subcmd returns rc=1") {
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("approach");
    ASSERT_TRUE(cmd != nullptr);
    std::streambuf* old = std::cerr.rdbuf();
    std::ostringstream sink;
    std::cerr.rdbuf(sink.rdbuf());
    int rc = cmd->run({"totally-unknown-subcmd-xyz"});
    std::cerr.rdbuf(old);
    ASSERT_EQ(rc, 1);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
