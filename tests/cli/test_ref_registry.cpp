// Phase 68 T2: unit tests for ref_registry session-scoped ID assignment.
#include "../test_main.hpp"
#include "../../src/cli/ref_registry.hpp"
#include <filesystem>

using icmg::cli::RefRegistry;
namespace fs = std::filesystem;

static std::string mkTmpRoot(const std::string& tag) {
    auto p = fs::temp_directory_path() / ("icmg_refreg_" + tag);
    fs::create_directories(p);
    fs::remove_all(p / ".icmg");  // clean prior state
    return p.string();
}

TEST("RefRegistry: first call assigns new ID") {
    auto root = mkTmpRoot("a");
    RefRegistry r(root);
    auto id = r.getOrAssign("MEM", "hello");
    ASSERT_TRUE(!id.empty());
    ASSERT_TRUE(id.find("[ICMG-MEM-") != std::string::npos);
}

TEST("RefRegistry: same content returns same ID") {
    auto root = mkTmpRoot("b");
    RefRegistry r(root);
    auto id1 = r.getOrAssign("MEM", "duplicate");
    auto id2 = r.getOrAssign("MEM", "duplicate");
    ASSERT_EQ(id1, id2);
}

TEST("RefRegistry: different content different ID") {
    auto root = mkTmpRoot("c");
    RefRegistry r(root);
    auto id1 = r.getOrAssign("MEM", "alpha");
    auto id2 = r.getOrAssign("MEM", "beta");
    ASSERT_TRUE(id1 != id2);
}

TEST("RefRegistry: kind separates namespaces") {
    auto root = mkTmpRoot("d");
    RefRegistry r(root);
    auto m1 = r.getOrAssign("MEM",  "x");
    auto g1 = r.getOrAssign("GR",   "x");
    ASSERT_TRUE(m1 != g1);
    ASSERT_TRUE(m1.find("MEM") != std::string::npos);
    ASSERT_TRUE(g1.find("GR") != std::string::npos);
}

TEST("RefRegistry: seen() returns false before assign") {
    auto root = mkTmpRoot("e");
    RefRegistry r(root);
    ASSERT_FALSE(r.seen("MEM", "fresh"));
    r.getOrAssign("MEM", "fresh");
    ASSERT_TRUE(r.seen("MEM", "fresh"));
}

TEST("RefRegistry: state persists across instances same day") {
    auto root = mkTmpRoot("f");
    std::string id1;
    {
        RefRegistry r(root);
        id1 = r.getOrAssign("MEM", "persist-me");
    }  // destructor flushes
    {
        RefRegistry r(root);
        ASSERT_TRUE(r.seen("MEM", "persist-me"));
        auto id2 = r.getOrAssign("MEM", "persist-me");
        ASSERT_EQ(id1, id2);
    }
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
