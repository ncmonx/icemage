// v1.78.4: updating.lock must be checked at icmg startup so new processes
// don't spawn and grab the exe file lock during a binary upgrade swap.
// Before the fix: no check existed — new processes spawned immediately after
// stopOrphanIcmgInstances(), causing rename(self, .bak) to fail.
#include "../test_main.hpp"
#include "../../src/core/update_lock.hpp"
#include <filesystem>
#include <fstream>
#include <ctime>
#include <cstdlib>

namespace fs = std::filesystem;

static fs::path testLockPath() {
    const char* p = std::getenv("USERPROFILE");
    if (!p) p = std::getenv("HOME");
    return fs::path(p ? p : ".") / ".icmg" / "updating.lock";
}

struct LockGuard {
    fs::path p;
    explicit LockGuard(const fs::path& path) : p(path) {}
    ~LockGuard() { std::error_code ec; fs::remove(p, ec); }
};

TEST("updating_lock: isUpdatingLockFresh returns false when no lock file") {
    LockGuard g(testLockPath());
    std::error_code ec;
    fs::remove(testLockPath(), ec); // ensure absent
    ASSERT_FALSE(icmg::core::isUpdatingLockFresh());
}

TEST("updating_lock: isUpdatingLockFresh returns true for fresh lock (<5min)") {
    auto lock = testLockPath();
    fs::create_directories(lock.parent_path());
    { std::ofstream f(lock); f << std::time(nullptr) << "\n"; }
    LockGuard g(lock);
    ASSERT_TRUE(icmg::core::isUpdatingLockFresh());
}

TEST("updating_lock: isUpdatingLockFresh returns false for stale lock (>5min)") {
    auto lock = testLockPath();
    fs::create_directories(lock.parent_path());
    { std::ofstream f(lock); f << (std::time(nullptr) - 400) << "\n"; } // 400s old
    LockGuard g(lock);
    ASSERT_FALSE(icmg::core::isUpdatingLockFresh());
}

TEST("updating_lock: isUpdatingLockFresh returns false for empty/corrupt lock") {
    auto lock = testLockPath();
    fs::create_directories(lock.parent_path());
    { std::ofstream f(lock); } // empty
    LockGuard g(lock);
    ASSERT_FALSE(icmg::core::isUpdatingLockFresh());
}
