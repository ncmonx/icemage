// v1.78.2 Phase D: per-project persist OFF marker file.
//
// `.icmg/cache-persist.off` lets users disable persist on a per-project basis
// without setting env. CLI sub-cmd `icmg memory cache persist disable` writes
// the marker; `enable` removes it.

#include "../test_main.hpp"
#include "../../src/core/recall_cache_persist.hpp"

#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace fs = std::filesystem;
using icmg::core::persistEnabledForRoot;

static fs::path mkTmpRoot(const std::string& tag) {
    auto p = fs::temp_directory_path() / ("rcp-marker-" + tag);
    fs::remove_all(p);
    fs::create_directories(p / ".icmg");
    return p;
}

static void clearEnv() {
#ifdef _WIN32
    _putenv_s("ICMG_RECALL_CACHE_PERSIST", "");
#else
    unsetenv("ICMG_RECALL_CACHE_PERSIST");
#endif
}

TEST("persist-marker: env=0 wins even when no marker file") {
    clearEnv();
    auto root = mkTmpRoot("envwins0");
#ifdef _WIN32
    _putenv_s("ICMG_RECALL_CACHE_PERSIST", "0");
#else
    setenv("ICMG_RECALL_CACHE_PERSIST", "0", 1);
#endif
    ASSERT_FALSE(persistEnabledForRoot(root.string()));
    clearEnv();
}

TEST("persist-marker: env=1 wins even when marker file present") {
    auto root = mkTmpRoot("envwins1");
    std::ofstream(root / ".icmg" / "cache-persist.off") << "off\n";
#ifdef _WIN32
    _putenv_s("ICMG_RECALL_CACHE_PERSIST", "1");
#else
    setenv("ICMG_RECALL_CACHE_PERSIST", "1", 1);
#endif
    ASSERT_TRUE(persistEnabledForRoot(root.string()));
    clearEnv();
}

TEST("persist-marker: env unset + no marker → default ON") {
    clearEnv();
    auto root = mkTmpRoot("defon");
    ASSERT_TRUE(persistEnabledForRoot(root.string()));
}

TEST("persist-marker: env unset + marker present → OFF") {
    clearEnv();
    auto root = mkTmpRoot("markeroff");
    std::ofstream(root / ".icmg" / "cache-persist.off") << "off\n";
    ASSERT_FALSE(persistEnabledForRoot(root.string()));
}

TEST("persist-marker: empty root → cwd-relative .icmg/cache-persist.off") {
    clearEnv();
    // No assertion about cwd state; just exercise the empty-root path (no crash).
    bool b = persistEnabledForRoot("");
    (void)b;
    ASSERT_TRUE(true);
}
