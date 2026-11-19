// #288: per-author memory visibility. Two OS accounts sharing one project
// folder share .icmg/data.db; `private` rows must stay with their author,
// `team` and legacy (author-less) rows stay shared.
#include "../test_main.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/memory_node.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/memory_visibility.hpp"
#include "../../src/core/migrator.hpp"
#include <chrono>
#include <filesystem>
#include <string>

using namespace icmg;
using namespace icmg::imem;
namespace fs = std::filesystem;

namespace {

constexpr const char* kAlice = "S-1-5-21-alice";
constexpr const char* kBob   = "S-1-5-21-bob";

// Pre-0047 schema: no author / visibility columns.
void legacySchema(core::Db& db) {
    db.run("CREATE TABLE IF NOT EXISTS memory_nodes("
           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " topic TEXT NOT NULL, content TEXT NOT NULL, keywords TEXT,"
           " importance INTEGER NOT NULL DEFAULT 1, frequency INTEGER NOT NULL DEFAULT 1,"
           " last_used INTEGER, created_at INTEGER, expires_at INTEGER, deleted_at INTEGER,"
           " zone TEXT NOT NULL DEFAULT 'default', pinned INTEGER NOT NULL DEFAULT 0,"
           " created_by TEXT NOT NULL DEFAULT '', git_sha TEXT NOT NULL DEFAULT '',"
           " row_version INTEGER NOT NULL DEFAULT 0)");
    db.run("CREATE TABLE IF NOT EXISTS memory_keywords(node_id INTEGER, keyword TEXT)");
}

// Full schema (recall needs query_history etc.).
void migrated(core::Db& db) {
    core::Migrator m("__nonexistent_migrations_dir__");
    m.runAll(db);
}

// Restores the real author even when an assertion throws.
struct AuthorGuard {
    explicit AuthorGuard(const char* a) { core::setCurrentAuthorForTest(a); }
    ~AuthorGuard() { core::setCurrentAuthorForTest(""); core::setSeeAllAuthors(false); }
};

int64_t storeAs(MemoryStore& ms, const char* author, const std::string& topic,
                const std::string& content, const std::string& vis = "") {
    core::setCurrentAuthorForTest(author);
    MemoryNode n; n.topic = topic; n.content = content; n.visibility = vis;
    return ms.store(n, /*force=*/true);
}

std::string col(core::Db& db, int64_t id, const char* name) {
    std::string out;
    db.query(std::string("SELECT ") + name + " FROM memory_nodes WHERE id=?", {std::to_string(id)},
             [&](const core::Row& r) { if (!r.empty()) out = r[0]; });
    return out;
}

bool recallHas(MemoryStore& ms, const std::string& q, int64_t id) {
    for (const auto& n : ms.recall(q, 20)) if (n.id == id) return true;
    return false;
}

}  // namespace

TEST("visibility: default from topic (session private, decisions team)") {
    ASSERT_EQ(std::string(core::defaultVisibilityForTopic("session:abc")), std::string("private"));
    ASSERT_EQ(std::string(core::defaultVisibilityForTopic("Memoir:x")), std::string("private"));
    ASSERT_EQ(std::string(core::defaultVisibilityForTopic("_feeling/latest")), std::string("private"));
    ASSERT_EQ(std::string(core::defaultVisibilityForTopic("decisions-build")), std::string("team"));
    ASSERT_EQ(std::string(core::defaultVisibilityForTopic("errors-resolved x")), std::string("team"));
    ASSERT_TRUE(core::parseVisibility("bogus") == nullptr);
}

TEST("visibility: canSee rule (own private, team, author-less)") {
    AuthorGuard g(kBob);
    ASSERT_TRUE(core::canSee("team", kAlice));
    ASSERT_TRUE(core::canSee("private", ""));
    ASSERT_TRUE(core::canSee("private", kBob));
    ASSERT_FALSE(core::canSee("private", kAlice));
    core::SeeAllAuthorsScope all(true);
    ASSERT_TRUE(core::canSee("private", kAlice));
}

TEST("visibility: store tags author and visibility") {
    AuthorGuard g(kAlice);
    core::Db db(":memory:"); legacySchema(db);
    MemoryStore ms(db);
    int64_t p = storeAs(ms, kAlice, "session:alice", "alice private zebra note");
    int64_t t = storeAs(ms, kAlice, "decisions-x", "team decision zebra", "");
    int64_t forced = storeAs(ms, kAlice, "decisions-y", "forced private zebra", "private");
    ASSERT_EQ(col(db, p, "author"), std::string(kAlice));
    ASSERT_EQ(col(db, p, "visibility"), std::string("private"));
    ASSERT_EQ(col(db, t, "visibility"), std::string("team"));
    ASSERT_EQ(col(db, forced, "visibility"), std::string("private"));
}

TEST("visibility: other user never sees private, still sees team") {
    AuthorGuard g(kAlice);
    core::Db db(":memory:"); migrated(db);
    MemoryStore ms(db);
    int64_t p = storeAs(ms, kAlice, "session:alice", "alice private zebra note");
    int64_t t = storeAs(ms, kAlice, "decisions-x", "team decision zebra");

    core::setCurrentAuthorForTest(kAlice);
    ASSERT_TRUE(recallHas(ms, "zebra", p));
    ASSERT_EQ(ms.get(p).id, p);

    core::setCurrentAuthorForTest(kBob);
    ASSERT_FALSE(recallHas(ms, "zebra", p));
    ASSERT_TRUE(recallHas(ms, "zebra", t));
    ASSERT_TRUE(ms.get(p).id != p);  // get() hides it too

    {   // explicit --all-authors lifts the filter, scoped
        core::SeeAllAuthorsScope all(true);
        ASSERT_TRUE(recallHas(ms, "zebra", p));
    }
    ASSERT_FALSE(recallHas(ms, "zebra", p));
}

TEST("visibility: untagged legacy rows become team and stay shared") {
    AuthorGuard g(kBob);
    core::Db db(":memory:"); legacySchema(db);
    db.run("INSERT INTO memory_nodes(topic,content,last_used,created_at) "
           "VALUES('session:old','legacy walrus row',1,1)");
    MemoryStore ms(db);  // guarded ALTER adds author/visibility
    int64_t id = 0;
    db.query("SELECT id FROM memory_nodes LIMIT 1", {}, [&](const core::Row& r) { id = std::stoll(r[0]); });
    ASSERT_EQ(col(db, id, "author"), std::string(""));
    ASSERT_EQ(col(db, id, "visibility"), std::string("team"));
    ASSERT_EQ(ms.get(id).id, id);  // visible to an unrelated account
}

TEST("visibility: migration 0047 adds columns, existing rows team") {
    core::Db db(":memory:"); migrated(db);
    bool hasAuthor = false, hasVis = false;
    db.query("PRAGMA table_info(memory_nodes)", {}, [&](const core::Row& r) {
        if (r.size() > 1 && r[1] == "author") hasAuthor = true;
        if (r.size() > 1 && r[1] == "visibility") hasVis = true;
    });
    ASSERT_TRUE(hasAuthor);
    ASSERT_TRUE(hasVis);
    db.run("INSERT INTO memory_nodes(topic,content,last_used,created_at) VALUES('x','y',1,1)");
    ASSERT_EQ(col(db, db.lastInsertId(), "visibility"), std::string("team"));
}

TEST("visibility: retag changes own + claims untagged, never others") {
    AuthorGuard g(kAlice);
    core::Db db(":memory:"); legacySchema(db);
    MemoryStore ms(db);
    int64_t alice = storeAs(ms, kAlice, "decisions-a", "alice team");
    db.run("INSERT INTO memory_nodes(topic,content,last_used,created_at) VALUES('notes-old','legacy',1,1)");
    int64_t legacy = db.lastInsertId();

    core::setCurrentAuthorForTest(kBob);
    ASSERT_EQ(core::retagMemories(db, {alice}, "", "private"), 0);
    ASSERT_EQ(col(db, alice, "visibility"), std::string("team"));
    ASSERT_EQ(core::retagMemories(db, {legacy}, "", "private"), 1);
    ASSERT_EQ(col(db, legacy, "author"), std::string(kBob));
    ASSERT_EQ(col(db, legacy, "visibility"), std::string("private"));

    core::setCurrentAuthorForTest(kAlice);
    ASSERT_EQ(core::retagMemories(db, {}, "decisions-", "private"), 1);
    ASSERT_EQ(col(db, alice, "visibility"), std::string("private"));
}

TEST("visibility: rememberDir prefers .remember/<user>") {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("icmg_rem_" + std::to_string(stamp));
    fs::create_directories(root / ".remember");
    ASSERT_EQ(core::rememberDir(root), root / ".remember");
    fs::create_directories(root / ".remember" / core::currentUserDirName());
    ASSERT_EQ(core::rememberDir(root), root / ".remember" / core::currentUserDirName());
    std::error_code ec;
    fs::remove_all(root, ec);
}
