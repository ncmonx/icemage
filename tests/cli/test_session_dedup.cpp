// Cache-hit optimizer #2: TTL-aware recall injection dedup. The old recall
// dedup was calendar-day scoped (RefRegistry) -> over-suppressed memory for a
// long-lived GUI across separate conversations. session_dedup gives it a TTL so
// re-injection within an active conversation is suppressed, but a later
// conversation re-surfaces the memory. Tests exercise the pure helpers.
#include "../test_main.hpp"
#include "../../src/cli/session_dedup.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace icmg::cli;

namespace {
std::string tmpPath(const char* stem) {
    auto p = std::filesystem::temp_directory_path() /
             (std::string("icmg_dedup_") + stem + "_test.txt");
    std::error_code ec; std::filesystem::remove(p, ec);
    return p.string();
}
}

TEST("session-dedup: unmarked id is not seen") {
    std::string f = tmpPath("a");
    ASSERT_FALSE(wasInjectedRecently(f, "42", 2700));
}

TEST("session-dedup: marked id is seen within TTL") {
    std::string f = tmpPath("b");
    markInjected(f, "42");
    ASSERT_TRUE(wasInjectedRecently(f, "42", 2700));
    // A different id is independent.
    ASSERT_FALSE(wasInjectedRecently(f, "99", 2700));
}

TEST("session-dedup: an entry older than TTL is ignored (resurfaces)") {
    std::string f = tmpPath("c");
    // Hand-write a stale entry stamped 1 hour ago.
    int64_t old_ts = (int64_t)std::time(nullptr) - 3600;
    { std::ofstream o(f); o << old_ts << "\t77\n"; }
    ASSERT_FALSE(wasInjectedRecently(f, "77", 1800));  // TTL 30 min < 1h age
    ASSERT_TRUE(wasInjectedRecently(f, "77", 7200));   // TTL 2h > 1h age
}

TEST("session-dedup: legacy untimestamped line matches once") {
    std::string f = tmpPath("d");
    { std::ofstream o(f); o << "55\n"; }  // no tab, no ts
    ASSERT_TRUE(wasInjectedRecently(f, "55", 1));
}

TEST("session-dedup: default TTL honors env override") {
#if defined(_WIN32)
    _putenv_s("ICMG_RECALL_DEDUP_TTL", "");          // unset -> default
    ASSERT_EQ((long long)recallDedupTTL(), 2700LL);
    _putenv_s("ICMG_RECALL_DEDUP_TTL", "600");
    ASSERT_EQ((long long)recallDedupTTL(), 600LL);
    _putenv_s("ICMG_RECALL_DEDUP_TTL", "");          // restore
#else
    unsetenv("ICMG_RECALL_DEDUP_TTL");
    ASSERT_EQ((long long)recallDedupTTL(), 2700LL);
    setenv("ICMG_RECALL_DEDUP_TTL", "600", 1);
    ASSERT_EQ((long long)recallDedupTTL(), 600LL);
    unsetenv("ICMG_RECALL_DEDUP_TTL");
#endif
}
