// test_focus_chain — unit tests for FocusChain CRUD.
#include "../test_main.hpp"
#include "../../src/imem/focus_chain.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include <string>
#include <vector>

using namespace icmg::core;
using namespace icmg::imem;

// Helper: in-memory fully-migrated DB.
static Db makeDb() {
    Db db(":memory:");
    Migrator migrator("__nonexistent_migrations_dir__");
    migrator.runAll(db);
    return db;
}

// 1. add then list returns the item with status="in", ord=1.
TEST("focus_chain: add then list returns item status=in ord=1") {
    auto db = makeDb();
    FocusChain fc(db);
    int64_t id = fc.add("session-A", "fix auth bug");
    ASSERT_TRUE(id > 0);
    auto items = fc.list("session-A");
    ASSERT_EQ(items.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(items[0].todo,       std::string("fix auth bug"));
    ASSERT_EQ(items[0].status,     std::string("in"));
    ASSERT_EQ(items[0].ord,        1);
    ASSERT_EQ(items[0].session_id, std::string("session-A"));
}

// 2. Second add → ord=2.
TEST("focus_chain: second add gets ord=2") {
    auto db = makeDb();
    FocusChain fc(db);
    fc.add("session-B", "first todo");
    int64_t id2 = fc.add("session-B", "second todo");
    ASSERT_TRUE(id2 > 0);
    auto items = fc.list("session-B");
    ASSERT_EQ(items.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(items[1].ord, 2);
    ASSERT_EQ(items[1].todo, std::string("second todo"));
}

// 3. setStatus "done" → list with status_filter="in" excludes it.
TEST("focus_chain: setStatus done excludes from in-filter") {
    auto db = makeDb();
    FocusChain fc(db);
    int64_t id = fc.add("session-C", "write recall test");
    ASSERT_TRUE(fc.setStatus(id, "done"));
    auto in_items  = fc.list("session-C", "in");
    auto all_items = fc.list("session-C", "");
    ASSERT_EQ(in_items.size(),  static_cast<std::size_t>(0));
    ASSERT_EQ(all_items.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(all_items[0].status, std::string("done"));
}

// 4. setStatus with bogus value returns false.
TEST("focus_chain: setStatus bogus returns false") {
    auto db = makeDb();
    FocusChain fc(db);
    int64_t id = fc.add("session-D", "some task");
    bool ok = fc.setStatus(id, "bogus");
    ASSERT_FALSE(ok);
    // Item should still be "in"
    auto items = fc.list("session-D");
    ASSERT_EQ(items.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(items[0].status, std::string("in"));
}

// 5. removeBySession clears all rows for that session.
TEST("focus_chain: removeBySession clears session rows") {
    auto db = makeDb();
    FocusChain fc(db);
    fc.add("session-E", "task one");
    fc.add("session-E", "task two");
    fc.add("session-F", "other task");
    bool ok = fc.removeBySession("session-E");
    ASSERT_TRUE(ok);
    auto e_items = fc.list("session-E");
    auto f_items = fc.list("session-F");
    ASSERT_EQ(e_items.size(), static_cast<std::size_t>(0));
    ASSERT_EQ(f_items.size(), static_cast<std::size_t>(1));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
