// test_task_cmd — failing tests for `icmg task` command (TDD, written before impl).
// 2026-06-26: task store — parked work items that survive across sessions + compaction.
#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include "../../src/core/config.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>

using namespace icmg::core;

// RAII guard: unique temp DB per test for full isolation.
struct TaskDbGuard {
    std::string path;
    explicit TaskDbGuard() {
        namespace fs = std::filesystem;
        path = (fs::temp_directory_path()
               / ("icmg_task_test_" + std::to_string(std::rand()) + ".db")).string();
        Config::instance().setProjectDbOverride(path);
    }
    ~TaskDbGuard() {
        Config::instance().clearProjectDbOverride();
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// 1. Registry: "task" command is registered.
TEST("task_cmd: registered in Registry") {
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("task");
    ASSERT_TRUE(cmd != nullptr);
}

// 2. Schema: task table exists after migration.
TEST("task_cmd: task table exists after migration") {
    Db db(":memory:");
    Migrator migrator("__nonexistent_migrations_dir__");
    migrator.runAll(db);
    int count = 0;
    db.query("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='task'", {},
        [&](const Row& r){ count = std::stoi(r[0]); });
    ASSERT_EQ(count, 1);
}

// 3. add then inject lists the task.
TEST("task_cmd: add then inject shows task") {
    TaskDbGuard g;
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("task");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"add", "implement covenant store"});
    ASSERT_EQ(rc, 0);
    std::ostringstream buf;
    std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
    cmd->run({"inject"});
    std::cout.rdbuf(old);
    ASSERT_CONTAINS(buf.str(), "implement covenant store");
}

// 4. inject with no open tasks emits nothing (quiet SessionStart-safe).
TEST("task_cmd: inject with empty store emits nothing") {
    TaskDbGuard g;
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("task");
    ASSERT_TRUE(cmd != nullptr);
    std::ostringstream buf;
    std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
    cmd->run({"inject"});
    std::cout.rdbuf(old);
    ASSERT_EQ(buf.str(), std::string(""));
}

// 5. task done → absent from inject (inject only shows open tasks).
TEST("task_cmd: done task absent from inject") {
    TaskDbGuard g;
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("task");
    ASSERT_TRUE(cmd != nullptr);
    cmd->run({"add", "open task"});
    cmd->run({"add", "done task"});
    cmd->run({"done", "2"});
    std::ostringstream buf;
    std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
    cmd->run({"inject"});
    std::cout.rdbuf(old);
    std::string out = buf.str();
    ASSERT_CONTAINS(out, "open task");
    ASSERT_NOT_CONTAINS(out, "done task");
}

// 6. doing status appears before todo in inject output.
TEST("task_cmd: inject emits doing before todo") {
    TaskDbGuard g;
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("task");
    ASSERT_TRUE(cmd != nullptr);
    cmd->run({"add", "task-todo-first-added"});
    cmd->run({"add", "task-doing-second-added"});
    cmd->run({"doing", "2"});
    std::ostringstream buf;
    std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
    cmd->run({"inject"});
    std::cout.rdbuf(old);
    std::string out = buf.str();
    auto pos_doing = out.find("task-doing-second-added");
    auto pos_todo  = out.find("task-todo-first-added");
    ASSERT_TRUE(pos_doing < pos_todo);
}

// 7. --max-items cap emits visible marker.
TEST("task_cmd: inject --max-items emits not-shown marker") {
    TaskDbGuard g;
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("task");
    ASSERT_TRUE(cmd != nullptr);
    cmd->run({"add", "t1"});
    cmd->run({"add", "t2"});
    cmd->run({"add", "t3"});
    std::ostringstream buf;
    std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
    cmd->run({"inject", "--max-items", "1"});
    std::cout.rdbuf(old);
    ASSERT_CONTAINS(buf.str(), "more");
}

// 8. reopen: done → todo; reappears in inject.
TEST("task_cmd: reopen makes done task reappear in inject") {
    TaskDbGuard g;
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("task");
    ASSERT_TRUE(cmd != nullptr);
    cmd->run({"add", "parked task"});
    cmd->run({"done", "1"});
    cmd->run({"reopen", "1"});
    std::ostringstream buf;
    std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
    cmd->run({"inject"});
    std::cout.rdbuf(old);
    ASSERT_CONTAINS(buf.str(), "parked task");
}

// 9. unknown subcmd returns non-zero.
TEST("task_cmd: unknown subcmd returns non-zero") {
    TaskDbGuard g;
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("task");
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_EQ(cmd->run({"unknownxyz"}), 1);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif

