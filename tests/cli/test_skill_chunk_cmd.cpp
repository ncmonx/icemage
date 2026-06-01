// test_skill_chunk_cmd — unit tests for `icmg skill chunk` subcommand.
//
// Tests:
//   1. skill command registered in registry.
//   2. `skill chunk <key> unknown-sub` returns rc=1.
//   3. `skill chunk <key> --list` on empty skill_chunks table returns rc=0.
//   4. `skill chunk <key> --reindex` populates skill_chunks rows.
//   5. `skill chunk <key> --get <path>` after reindex returns content.

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

namespace cli  = icmg::cli;
namespace core = icmg::core;

// ---- helpers ----------------------------------------------------------------

static core::Db makeFullyMigratedDb() {
    core::Db db(":memory:");
    core::Migrator migrator("__nonexistent_migrations_dir__");
    migrator.runAll(db);
    return db;
}

// Seed a skill node into context_nodes directly and return its id string.
static std::string seedSkill(core::Db& db,
                              const std::string& node_key,
                              const std::string& content,
                              const std::string& source_file = "fake.md") {
    db.run(
        "INSERT OR REPLACE INTO context_nodes"
        "(node_key, title, content, source_file, tier, tags, active)"
        " VALUES (?, ?, ?, ?, 'skill', '[]', 1)",
        {node_key, node_key, content, source_file}
    );
    std::string id;
    db.query(
        "SELECT id FROM context_nodes WHERE node_key=?",
        {node_key},
        [&](const core::Row& row) { if (!row.empty()) id = row[0]; }
    );
    return id;
}

// Count rows in skill_chunks for a given skill_id string.
static int countChunks(core::Db& db, const std::string& skill_id) {
    int n = 0;
    db.query(
        "SELECT COUNT(*) FROM skill_chunks WHERE skill_id=?",
        {skill_id},
        [&](const core::Row& row) {
            if (!row.empty()) n = std::stoi(row[0]);
        }
    );
    return n;
}

// ---- TEST 1: command registered ---------------------------------------------

TEST("skill chunk registered: Registry::create(\"skill\") returns non-null") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));
}

// ---- TEST 2: unknown subcmd returns 1 --------------------------------------
// Supply a skill_key so the DB-fail-soft path (no key = SessionStart hook)
// does not trigger; instead the command tries to open the DB, fails (no
// .icmg/ in build CWD), and returns 1 with an error message, OR it opens the
// DB but the skill_key is not found → rc=1.
// Either way rc must be 1.

TEST("skill chunk: chunk with unknown skill_key returns 1") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));

    std::streambuf* orig_err = std::cerr.rdbuf();
    std::ostringstream cap_err;
    std::cerr.rdbuf(cap_err.rdbuf());

    // Use a DB override pointing at a temp in-memory-like path so we control
    // whether the skill exists. Supply a key that is not seeded → rc=1.
    std::string tmp_path = std::tmpnam(nullptr);
    tmp_path += "_icmg_test_unknown_skill.db";
    {
        core::Db tmp_db(tmp_path);
        core::Migrator migrator("__nonexistent_migrations_dir__");
        migrator.runAll(tmp_db);
        // Do NOT seed the skill — we want "not found".
    }

    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride(tmp_path);

    int rc = cmd->run({"chunk", "nonexistent-skill-key"});

    cfg.clearProjectDbOverride();
    std::cerr.rdbuf(orig_err);

    ASSERT_EQ(rc, 1);
    ASSERT_CONTAINS(cap_err.str(), "not found");
}

// ---- TEST 3: --list empty when no chunks -----------------------------------
// Fresh project DB; seed 1 skill via direct insert; run chunk --list → rc=0.

TEST("skill chunk --list empty when no chunks: rc=0, no crash") {
    auto db = makeFullyMigratedDb();

    seedSkill(db, "test-skill-empty", "# Test\n\nSome intro text.\n");

    // Point Config override at this :memory: DB — but manifest/chunk opens
    // DB via Config::projectDbPath. We need a real temp file here so that
    // the command can open it.
    // Strategy: use setProjectDbOverride to give the command our temp path.
    // Since we need the command to open the same DB we seeded, we write to
    // a temp file-backed DB.

    // Use a named temp file so the command can open it.
    std::string tmp_path = std::tmpnam(nullptr);
    tmp_path += "_icmg_test_chunk.db";

    {
        core::Db tmp_db(tmp_path);
        core::Migrator migrator("__nonexistent_migrations_dir__");
        migrator.runAll(tmp_db);
        seedSkill(tmp_db, "test-skill-empty", "# Test\n\nSome intro text.\n");
    }

    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride(tmp_path);

    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));

    std::streambuf* orig_out = std::cout.rdbuf();
    std::ostringstream cap_out;
    std::cout.rdbuf(cap_out.rdbuf());

    int rc = cmd->run({"chunk", "test-skill-empty", "--list"});

    std::cout.rdbuf(orig_out);
    cfg.clearProjectDbOverride();

    ASSERT_EQ(rc, 0);
}

// ---- TEST 4: --reindex populates skill_chunks ------------------------------

TEST("skill chunk --reindex populates skill_chunks rows") {
    std::string tmp_path = std::tmpnam(nullptr);
    tmp_path += "_icmg_test_reindex.db";

    const std::string content =
        "# My Skill\n\n"
        "Intro paragraph.\n\n"
        "## Section One\n\n"
        "Content of section one.\n\n"
        "## Section Two\n\n"
        "Content of section two.\n";

    std::string skill_id;
    {
        core::Db tmp_db(tmp_path);
        core::Migrator migrator("__nonexistent_migrations_dir__");
        migrator.runAll(tmp_db);
        skill_id = seedSkill(tmp_db, "my-skill", content);
    }

    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride(tmp_path);

    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));

    std::streambuf* orig_out = std::cout.rdbuf();
    std::ostringstream cap_out;
    std::cout.rdbuf(cap_out.rdbuf());

    int rc = cmd->run({"chunk", "my-skill", "--reindex"});

    std::cout.rdbuf(orig_out);
    cfg.clearProjectDbOverride();

    ASSERT_EQ(rc, 0);

    // Verify rows inserted: expect 2 or 3 rows (intro + 2 H2 sections)
    core::Db check_db(tmp_path);
    int n = countChunks(check_db, skill_id);
    ASSERT_TRUE(n >= 2);
}

// ---- TEST 5: --get returns content for path --------------------------------

TEST("skill chunk --get returns content for path after reindex") {
    std::string tmp_path = std::tmpnam(nullptr);
    tmp_path += "_icmg_test_get.db";

    const std::string content =
        "# My Skill\n\n"
        "Intro paragraph.\n\n"
        "## Section Alpha\n\n"
        "Content of section alpha.\n\n"
        "## Section Beta\n\n"
        "Content of section beta.\n";

    {
        core::Db tmp_db(tmp_path);
        core::Migrator migrator("__nonexistent_migrations_dir__");
        migrator.runAll(tmp_db);
        seedSkill(tmp_db, "my-get-skill", content);
    }

    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride(tmp_path);

    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));

    // First reindex
    {
        std::streambuf* orig_out = std::cout.rdbuf();
        std::ostringstream cap;
        std::cout.rdbuf(cap.rdbuf());
        cmd->run({"chunk", "my-get-skill", "--reindex"});
        std::cout.rdbuf(orig_out);
    }

    // Find a valid parent_path from the DB
    std::string path_to_get;
    {
        core::Db check_db(tmp_path);
        check_db.query(
            "SELECT sc.parent_path FROM skill_chunks sc"
            " JOIN context_nodes cn ON cn.id=sc.skill_id"
            " WHERE cn.node_key='my-get-skill'"
            " LIMIT 1",
            {},
            [&](const core::Row& row) {
                if (!row.empty()) path_to_get = row[0];
            }
        );
    }
    ASSERT_TRUE(!path_to_get.empty());

    // Now --get
    std::streambuf* orig_out = std::cout.rdbuf();
    std::ostringstream cap_out;
    std::cout.rdbuf(cap_out.rdbuf());

    int rc = cmd->run({"chunk", "my-get-skill", "--get", path_to_get});

    std::cout.rdbuf(orig_out);
    cfg.clearProjectDbOverride();

    ASSERT_EQ(rc, 0);
    // Output should be non-empty (raw chunk content)
    ASSERT_TRUE(!cap_out.str().empty());
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
