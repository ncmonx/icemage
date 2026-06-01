// test_skill_ask_cmd — unit tests for `icmg skill ask` subcommand.
//
// Tests:
//   1. skill ask registered, unknown subcmd (no query) → rc=1 with usage.
//   2. skill ask empty DB → rc=0, no output.
//   3. skill ask seeded chunks returns matching content (JSON, first result contains "waris"/"pewarisan").
//   4. skill ask --skill filter restricts results to one skill.
//   5. skill ask --top caps output to N results.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include "../../src/core/config.hpp"

#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>

namespace cli  = icmg::cli;
namespace core = icmg::core;

// ---- helpers ----------------------------------------------------------------

static core::Db makeFullyMigratedDb(const std::string& path) {
    core::Db db(path);
    core::Migrator migrator("__nonexistent_migrations_dir__");
    migrator.runAll(db);
    return db;
}

// Seed a skill node; return its id string.
static std::string seedSkill(core::Db& db,
                              const std::string& node_key,
                              const std::string& content = "skill content") {
    db.run(
        "INSERT OR REPLACE INTO context_nodes"
        "(node_key, title, content, source_file, tier, tags, active)"
        " VALUES (?, ?, ?, 'fake.md', 'skill', '[]', 1)",
        {node_key, node_key, content}
    );
    std::string id;
    db.query(
        "SELECT id FROM context_nodes WHERE node_key=?",
        {node_key},
        [&](const core::Row& row) { if (!row.empty()) id = row[0]; }
    );
    return id;
}

// Insert a chunk row directly (no embedding).
static void insertChunk(core::Db& db,
                        const std::string& skill_id,
                        const std::string& parent_path,
                        const std::string& heading,
                        const std::string& content) {
    db.run(
        "INSERT INTO skill_chunks(skill_id, parent_path, heading, content, token_count)"
        " VALUES (?, ?, ?, ?, 10)",
        {skill_id, parent_path, heading, content}
    );
}

// ---- TEST 1: ask with no query returns rc=1 ---------------------------------

TEST("skill ask: no query arg returns rc=1") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));

    std::streambuf* orig_err = std::cerr.rdbuf();
    std::ostringstream cap_err;
    std::cerr.rdbuf(cap_err.rdbuf());

    std::streambuf* orig_out = std::cout.rdbuf();
    std::ostringstream cap_out;
    std::cout.rdbuf(cap_out.rdbuf());

    // Provide a real (migratable) DB so we don't hit DB-open fail
    std::string tmp = std::string(std::tmpnam(nullptr)) + "_ask_noquery.db";
    {
        core::Db tmp_db(tmp);
        core::Migrator migrator("__nonexistent_migrations_dir__");
        migrator.runAll(tmp_db);
    }
    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride(tmp);

    int rc = cmd->run({"ask"});

    std::cout.rdbuf(orig_out);
    std::cerr.rdbuf(orig_err);
    cfg.clearProjectDbOverride();

    ASSERT_EQ(rc, 1);
}

// ---- TEST 2: ask on empty DB returns rc=0 -----------------------------------

TEST("skill ask: empty DB returns rc=0, no output") {
    std::string tmp = std::string(std::tmpnam(nullptr)) + "_ask_empty.db";
    {
        core::Db tmp_db(tmp);
        core::Migrator migrator("__nonexistent_migrations_dir__");
        migrator.runAll(tmp_db);
        // No rows seeded
    }

    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride(tmp);

    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));

    std::streambuf* orig_out = std::cout.rdbuf();
    std::ostringstream cap_out;
    std::cout.rdbuf(cap_out.rdbuf());

    int rc = cmd->run({"ask", "test"});

    std::cout.rdbuf(orig_out);
    cfg.clearProjectDbOverride();

    ASSERT_EQ(rc, 0);
}

// ---- TEST 3: seeded chunks returns matching content -------------------------

TEST("skill ask: seeded chunks returns content matching query term") {
    std::string tmp = std::string(std::tmpnam(nullptr)) + "_ask_seeded.db";
    {
        core::Db tmp_db(tmp);
        core::Migrator migrator("__nonexistent_migrations_dir__");
        migrator.runAll(tmp_db);

        std::string id1 = seedSkill(tmp_db, "skill-hukum-perdata");
        insertChunk(tmp_db, id1, "skill-hukum-perdata/1", "Ahli Waris",
                    "ahli waris pewarisan harta benda");
        insertChunk(tmp_db, id1, "skill-hukum-perdata/2", "Sewa Menyewa",
                    "perjanjian sewa menyewa properti");
        insertChunk(tmp_db, id1, "skill-hukum-perdata/3", "Hukum Dagang",
                    "hukum dagang perdagangan internasional");
    }

    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride(tmp);

    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));

    std::streambuf* orig_out = std::cout.rdbuf();
    std::ostringstream cap_out;
    std::cout.rdbuf(cap_out.rdbuf());

    int rc = cmd->run({"ask", "waris", "--top", "3", "--json"});

    std::cout.rdbuf(orig_out);
    cfg.clearProjectDbOverride();

    ASSERT_EQ(rc, 0);
    std::string out = cap_out.str();
    // First result should mention "waris" or "pewarisan"
    ASSERT_TRUE(out.find("waris") != std::string::npos ||
                out.find("pewarisan") != std::string::npos);
}

// ---- TEST 4: --skill filter restricts results to one skill ------------------

TEST("skill ask: --skill filter restricts results to that skill only") {
    std::string tmp = std::string(std::tmpnam(nullptr)) + "_ask_filter.db";
    {
        core::Db tmp_db(tmp);
        core::Migrator migrator("__nonexistent_migrations_dir__");
        migrator.runAll(tmp_db);

        // Skill 1
        std::string id1 = seedSkill(tmp_db, "skill-alpha");
        insertChunk(tmp_db, id1, "skill-alpha/1", "Section Alpha",
                    "common word query target alpha content");

        // Skill 2
        std::string id2 = seedSkill(tmp_db, "skill-beta");
        insertChunk(tmp_db, id2, "skill-beta/1", "Section Beta",
                    "common word query target beta content");
    }

    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride(tmp);

    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));

    std::streambuf* orig_out = std::cout.rdbuf();
    std::ostringstream cap_out;
    std::cout.rdbuf(cap_out.rdbuf());

    // Ask with --skill skill-alpha, query "query"
    int rc = cmd->run({"ask", "query", "--skill", "skill-alpha", "--json"});

    std::cout.rdbuf(orig_out);
    cfg.clearProjectDbOverride();

    ASSERT_EQ(rc, 0);
    std::string out = cap_out.str();
    // Results must contain "skill-alpha" and NOT "skill-beta"
    ASSERT_TRUE(out.find("skill-alpha") != std::string::npos);
    ASSERT_TRUE(out.find("skill-beta") == std::string::npos);
}

// ---- TEST 5: --top caps output to N results ---------------------------------

TEST("skill ask: --top caps output to specified count") {
    std::string tmp = std::string(std::tmpnam(nullptr)) + "_ask_top.db";
    {
        core::Db tmp_db(tmp);
        core::Migrator migrator("__nonexistent_migrations_dir__");
        migrator.runAll(tmp_db);

        std::string id1 = seedSkill(tmp_db, "skill-cap-test");
        // Insert 10 chunks all containing "testword"
        for (int i = 1; i <= 10; ++i) {
            std::string path = "skill-cap-test/" + std::to_string(i);
            std::string heading = "Section " + std::to_string(i);
            std::string content = "testword chunk number " + std::to_string(i)
                                + " contains the testword search term here";
            insertChunk(tmp_db, id1, path, heading, content);
        }
    }

    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride(tmp);

    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));

    std::streambuf* orig_out = std::cout.rdbuf();
    std::ostringstream cap_out;
    std::cout.rdbuf(cap_out.rdbuf());

    int rc = cmd->run({"ask", "testword", "--top", "2", "--json"});

    std::cout.rdbuf(orig_out);
    cfg.clearProjectDbOverride();

    ASSERT_EQ(rc, 0);
    std::string out = cap_out.str();
    // JSON array should have exactly 2 objects: count occurrences of "path"
    // (each JSON object has exactly one "path" key)
    int path_count = 0;
    size_t pos = 0;
    while ((pos = out.find("\"path\"", pos)) != std::string::npos) {
        ++path_count;
        pos += 6;
    }
    ASSERT_EQ(path_count, 2);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
