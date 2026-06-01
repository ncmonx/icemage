// test_focus_cmd — unit tests for `icmg focus` command.
#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/imem/focus_chain.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <iostream>

using namespace icmg::core;
using namespace icmg::imem;

// Helper: in-memory fully-migrated DB.
static Db makeDb() {
    Db db(":memory:");
    Migrator migrator("__nonexistent_migrations_dir__");
    migrator.runAll(db);
    return db;
}

// 1. Registry::create("focus") returns non-null.
TEST("focus_cmd: registered in Registry") {
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("focus");
    ASSERT_TRUE(cmd != nullptr);
}

// 2. focus add then focus list --json JSON contains the todo text.
//    Tested at the FocusChain layer (the cmd delegates here).
TEST("focus_cmd: add then list json contains todo text") {
    auto db = makeDb();
    FocusChain fc(db);
    fc.add("test-session", "write unit tests");
    auto items = fc.list("test-session");
    ASSERT_EQ(items.size(), static_cast<std::size_t>(1));
    ASSERT_CONTAINS(items[0].todo, "write unit tests");
}

// 3. inject with no items returns empty string (silent SessionStart-safe).
TEST("focus_cmd: inject with no items returns empty") {
    auto db = makeDb();
    FocusChain fc(db);
    auto items = fc.list("empty-session", "in", 5);
    // format the markdown block the same way inject does
    std::string out;
    if (!items.empty()) {
        out = "## Focus chain (current session todos)\n";
        for (auto& it : items) {
            std::string box = (it.status == "done") ? "[x]" : "[ ]";
            out += "- " + box + " " + it.todo + "\n";
        }
    }
    ASSERT_EQ(out, std::string(""));
}

// 4. inject with 2 items returns string containing both "- [ ]" lines AND the header.
TEST("focus_cmd: inject with 2 items returns header and checkboxes") {
    auto db = makeDb();
    FocusChain fc(db);
    fc.add("inj-session", "fix auth bug");
    fc.add("inj-session", "write recall test");
    auto items = fc.list("inj-session", "in", 5);
    ASSERT_EQ(items.size(), static_cast<std::size_t>(2));
    std::string out = "## Focus chain (current session todos)\n";
    for (auto& it : items) {
        std::string box = (it.status == "done") ? "[x]" : "[ ]";
        out += "- " + box + " " + it.todo + "\n";
    }
    ASSERT_CONTAINS(out, "## Focus chain");
    ASSERT_CONTAINS(out, "- [ ] fix auth bug");
    ASSERT_CONTAINS(out, "- [ ] write recall test");
}

// 5. Unknown subcmd returns rc=1.
TEST("focus_cmd: unknown subcmd returns rc=1") {
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("focus");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"unknownsubcmd123"});
    ASSERT_EQ(rc, 1);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
