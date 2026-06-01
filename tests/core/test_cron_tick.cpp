// M6 T1: daemon cron-tick integration tests.
// Verifies CronStore::dueJobs + markRan contract used by maintenance thread.
// Does NOT test the thread itself (avoid timing flakiness).
#include "../test_main.hpp"
#include "../../src/core/cron_store.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
namespace core = icmg::core;

static std::string tmpDb(const char* tag) {
    auto p = fs::temp_directory_path() / (std::string("icmg_cron_tick_") + tag + ".db");
    std::error_code ec; fs::remove(p, ec);
    return p.string();
}

static int64_t nowSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

TEST("cron_tick: overdue job appears in dueJobs") {
    core::CronStore cs(tmpDb("due"));
    int64_t n = nowSec();
    cs.upsert("/proj", "memory prune-old", 60);
    cs.markRan("/proj", "memory prune-old", n - 7200); // last ran 2h ago
    auto due = cs.dueJobs(n);
    ASSERT_TRUE(!due.empty());
    ASSERT_EQ(due[0].chore, std::string("memory prune-old"));
}

TEST("cron_tick: recently-run job NOT in dueJobs") {
    core::CronStore cs(tmpDb("notdue"));
    int64_t n = nowSec();
    cs.upsert("/proj", "memory prune-old", 60);
    cs.markRan("/proj", "memory prune-old", n - 10); // ran 10s ago
    auto due = cs.dueJobs(n);
    ASSERT_TRUE(due.empty());
}

TEST("cron_tick: markRan removes job from due list") {
    core::CronStore cs(tmpDb("markran"));
    int64_t n = nowSec();
    cs.upsert("/proj", "backup snapshot", 30);
    cs.markRan("/proj", "backup snapshot", n - 7200); // overdue
    ASSERT_TRUE(!cs.dueJobs(n).empty());
    cs.markRan("/proj", "backup snapshot", n);
    ASSERT_TRUE(cs.dueJobs(n).empty());
}

TEST("cron_tick: multiple projects tracked independently") {
    core::CronStore cs(tmpDb("multi"));
    int64_t n = nowSec();
    cs.upsert("/a", "job", 60); cs.markRan("/a", "job", n - 7200); // overdue
    cs.upsert("/b", "job", 60); cs.markRan("/b", "job", n - 10);   // not due
    auto due = cs.dueJobs(n);
    ASSERT_EQ((int)due.size(), 1);
    ASSERT_EQ(due[0].project_path, std::string("/a"));
}
