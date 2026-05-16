// test_hook_skill_suggest — unit tests for runUserPromptSkillSuggest (T7).
//
// Tests:
//   1. Empty prompt → empty string.
//   2. No DB (or no skill_chunks) → empty fail-soft.
//   3. Matching chunk above threshold → hint block with expected text.
//   4. ICMG_SKILL_QUIET=1 → empty string.
//   5. Below-threshold chunk → empty string.

#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include "../../src/core/config.hpp"
#include "../../src/core/hooks/internals.hpp"

#include <cstdlib>
#include <string>

namespace core = icmg::core;

// ---- helpers ----------------------------------------------------------------

static core::Db makeFullyMigratedDb(const std::string& path) {
    core::Db db(path);
    core::Migrator migrator("__nonexistent_migrations_dir__");
    migrator.runAll(db);
    return db;
}

// Insert a skill node; return its rowid string.
static std::string seedSkillNode(core::Db& db, const std::string& node_key) {
    db.run(
        "INSERT OR REPLACE INTO context_nodes"
        "(node_key, title, content, source_file, tier, tags, active)"
        " VALUES (?, ?, 'skill body', 'fake.md', 'skill', '[]', 1)",
        {node_key, node_key}
    );
    std::string id;
    db.query(
        "SELECT id FROM context_nodes WHERE node_key=?",
        {node_key},
        [&](const core::Row& row) { if (!row.empty()) id = row[0]; }
    );
    return id;
}

// Insert a skill_chunk row (no embedding — BM25-only path).
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

// ---- TEST 1: empty prompt → empty ------------------------------------------

TEST("runUserPromptSkillSuggest empty prompt → empty") {
    std::string out = icmg::core::hooks::runUserPromptSkillSuggest("");
    ASSERT_EQ(out, std::string(""));
}

// ---- TEST 2: no DB → empty fail-soft ----------------------------------------

TEST("runUserPromptSkillSuggest no DB → empty fail-soft") {
    // Point DB override to a non-existent path.
    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride("/tmp/icmg-test-T7-nonexistent/data.db");

    std::string out = icmg::core::hooks::runUserPromptSkillSuggest("siapa ahli waris");

    cfg.clearProjectDbOverride();

    ASSERT_EQ(out, std::string(""));
}

// ---- TEST 3: matching chunk above threshold → block -------------------------

TEST("runUserPromptSkillSuggest matching chunk above threshold → block") {
    std::string tmp = std::string(std::tmpnam(nullptr)) + "_T7_match.db";
    {
        core::Db tmp_db(tmp);
        core::Migrator migrator("__nonexistent_migrations_dir__");
        migrator.runAll(tmp_db);

        std::string id = seedSkillNode(tmp_db, "kuhperdata");
        // Seed 2+ chunks so BM25 log(N/df) is non-zero for the matching one.
        // With N=2, a term in only 1 doc → log(2/1)=0.693. The matching chunk
        // scores higher; the decoy shares no tokens with the query.
        insertChunk(tmp_db, id, "kuhperdata/waris", "## Pewarisan Harta",
                    "pewarisan harta waris pasal 528 ahli waris warisan");
        insertChunk(tmp_db, id, "kuhperdata/kontrak", "## Perjanjian Kontrak",
                    "perjanjian kontrak bisnis dagang perdagangan ekspor impor");
    }

    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride(tmp);

    std::string out = icmg::core::hooks::runUserPromptSkillSuggest("siapa ahli waris");

    cfg.clearProjectDbOverride();

    // Must be non-empty and contain expected markers.
    ASSERT_TRUE(!out.empty());
    ASSERT_CONTAINS(out, "## Skill hint");
    ASSERT_CONTAINS(out, "icmg context");
    ASSERT_CONTAINS(out, "pewarisan");
}

// ---- TEST 4: ICMG_SKILL_QUIET=1 → empty ------------------------------------

TEST("runUserPromptSkillSuggest ICMG_SKILL_QUIET=1 → empty") {
    std::string tmp = std::string(std::tmpnam(nullptr)) + "_T7_quiet.db";
    {
        core::Db tmp_db(tmp);
        core::Migrator migrator("__nonexistent_migrations_dir__");
        migrator.runAll(tmp_db);

        std::string id = seedSkillNode(tmp_db, "kuhperdata-quiet");
        insertChunk(tmp_db, id, "kuhperdata-quiet/waris", "## Pewarisan",
                    "pewarisan harta waris pasal 528 ahli waris warisan");
        insertChunk(tmp_db, id, "kuhperdata-quiet/kontrak", "## Kontrak",
                    "perjanjian kontrak bisnis dagang perdagangan");
    }

    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride(tmp);

#ifdef _WIN32
    _putenv_s("ICMG_SKILL_QUIET", "1");
#else
    setenv("ICMG_SKILL_QUIET", "1", 1);
#endif

    std::string out = icmg::core::hooks::runUserPromptSkillSuggest("siapa ahli waris");

#ifdef _WIN32
    _putenv_s("ICMG_SKILL_QUIET", "");
#else
    unsetenv("ICMG_SKILL_QUIET");
#endif

    cfg.clearProjectDbOverride();

    ASSERT_EQ(out, std::string(""));
}

// ---- TEST 5: below-threshold → empty ----------------------------------------

TEST("runUserPromptSkillSuggest below-threshold → empty") {
    std::string tmp = std::string(std::tmpnam(nullptr)) + "_T7_low.db";
    {
        core::Db tmp_db(tmp);
        core::Migrator migrator("__nonexistent_migrations_dir__");
        migrator.runAll(tmp_db);

        // Seed 2 chunks so BM25 denominators are valid, but neither shares
        // tokens with the query — final score will be 0 < 0.20 threshold.
        std::string id = seedSkillNode(tmp_db, "unrelated-skill");
        insertChunk(tmp_db, id, "unrelated-skill/1", "## Unrelated Alpha",
                    "completely unrelated content alpha nothing matches here");
        insertChunk(tmp_db, id, "unrelated-skill/2", "## Unrelated Beta",
                    "completely unrelated content beta nothing matches here");
    }

    auto& cfg = core::Config::instance();
    cfg.setProjectDbOverride(tmp);

    // Query tokens share no terms with any chunk content — BM25 score = 0.
    std::string out = icmg::core::hooks::runUserPromptSkillSuggest("zxqvwm jupiter neptune saturn");

    cfg.clearProjectDbOverride();

    ASSERT_EQ(out, std::string(""));
}

int main() {
    std::cout << "=== hook_skill_suggest tests ===\n";
    return icmg::test::run_all();
}
