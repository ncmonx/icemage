// v1.10.0 T4: CronStore unit tests.

#include "../test_main.hpp"
#include "../../src/core/cron_store.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
namespace core = icmg::core;

namespace {

// Use unique temp DB per test process.
std::string tmpDbPath(const std::string& tag) {
    auto t = fs::temp_directory_path() / ("icmg_test_cron_" + tag + ".db");
    std::error_code ec;
    fs::remove(t, ec);
    return t.string();
}

bool findChore(const std::vector<core::CronJob>& jobs,
               const std::string& proj, const std::string& chore) {
    return std::any_of(jobs.begin(), jobs.end(),
        [&](const core::CronJob& j) {
            return j.project_path == proj && j.chore == chore;
        });
}

}  // namespace

TEST("CronStore: empty listAll returns no rows") {
    core::CronStore cs(tmpDbPath("empty"));
    auto jobs = cs.listAll();
    ASSERT_EQ(jobs.size(), (size_t)0);
}

TEST("CronStore: upsert + listAll roundtrip") {
    core::CronStore cs(tmpDbPath("upsert"));
    cs.upsert("/proj/a", "backup snapshot", 60);
    cs.upsert("/proj/a", "maintain run",    360);
    auto jobs = cs.listAll();
    ASSERT_EQ(jobs.size(), (size_t)2);
    ASSERT_TRUE(findChore(jobs, "/proj/a", "backup snapshot"));
    ASSERT_TRUE(findChore(jobs, "/proj/a", "maintain run"));
}

TEST("CronStore: upsert is idempotent on conflict") {
    core::CronStore cs(tmpDbPath("idem"));
    cs.upsert("/proj/x", "mirror sync", 15);
    cs.upsert("/proj/x", "mirror sync", 30);  // bump interval
    auto jobs = cs.listAll();
    ASSERT_EQ(jobs.size(), (size_t)1);
    ASSERT_EQ(jobs[0].every_min, 30);
}

TEST("CronStore: remove single chore") {
    core::CronStore cs(tmpDbPath("rm1"));
    cs.upsert("/proj/y", "backup snapshot", 60);
    cs.upsert("/proj/y", "mirror sync",     15);
    cs.remove("/proj/y", "mirror sync");
    auto jobs = cs.listAll();
    ASSERT_EQ(jobs.size(), (size_t)1);
    ASSERT_EQ(jobs[0].chore, std::string("backup snapshot"));
}

TEST("CronStore: removeProject drops all chores for project") {
    core::CronStore cs(tmpDbPath("rmproj"));
    cs.upsert("/proj/z1", "backup snapshot", 60);
    cs.upsert("/proj/z1", "maintain run",    360);
    cs.upsert("/proj/z2", "mirror sync",     15);
    cs.removeProject("/proj/z1");
    auto jobs = cs.listAll();
    ASSERT_EQ(jobs.size(), (size_t)1);
    ASSERT_EQ(jobs[0].project_path, std::string("/proj/z2"));
}

TEST("CronStore: dueJobs respects last_run + every_min interval") {
    core::CronStore cs(tmpDbPath("due"));
    cs.upsert("/proj/d", "backup snapshot", 60);
    // last_run == 0 → due at any now > 0
    int64_t now = 100000;
    auto due0 = cs.dueJobs(now);
    ASSERT_TRUE(due0.size() >= (size_t)1);
    // Mark ran just now → not due immediately after
    cs.markRan("/proj/d", "backup snapshot", now);
    auto due1 = cs.dueJobs(now + 30);  // 30s later — interval is 60min
    ASSERT_EQ(due1.size(), (size_t)0);
    // 61 min later → due again
    auto due2 = cs.dueJobs(now + 61 * 60);
    ASSERT_TRUE(due2.size() >= (size_t)1);
}

TEST("CronStore: markRan updates last_run") {
    core::CronStore cs(tmpDbPath("mark"));
    cs.upsert("/proj/m", "sentinel run", 15);
    cs.markRan("/proj/m", "sentinel run", 9999);
    auto jobs = cs.listAll();
    ASSERT_EQ(jobs.size(), (size_t)1);
    ASSERT_EQ(jobs[0].last_run, (int64_t)9999);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
