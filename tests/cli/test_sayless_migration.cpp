// v1.78.1: caveman → sayless flag-path auto-migration (called by `icmg init --force`).

#include "../test_main.hpp"
#include "../../src/cli/sayless_migrate.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace icmg::cli;

static fs::path tmpDir(const std::string& tag) {
    auto p = fs::temp_directory_path() / ("sayless-mig-" + tag + "-" + std::to_string((unsigned long long)std::hash<std::string>{}(tag)));
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

TEST("sayless-migrate: no-op when nothing to migrate") {
    auto root = tmpDir("noop");
    fs::create_directories(root / ".icmg");
    int migrated = migrateCavemanToSayless(root);
    ASSERT_EQ(migrated, 0);
    ASSERT_FALSE(fs::exists(root / ".icmg" / "sayless.flag"));
}

TEST("sayless-migrate: project caveman.flag -> sayless.flag with content preserved") {
    auto root = tmpDir("proj-on");
    fs::create_directories(root / ".icmg");
    std::ofstream(root / ".icmg" / "caveman.flag") << "ultra\n";
    int migrated = migrateCavemanToSayless(root);
    ASSERT_EQ(migrated, 1);
    ASSERT_TRUE(fs::exists(root / ".icmg" / "sayless.flag"));
    ASSERT_FALSE(fs::exists(root / ".icmg" / "caveman.flag"));
    std::ifstream f(root / ".icmg" / "sayless.flag");
    std::string lvl; std::getline(f, lvl);
    ASSERT_EQ(lvl, std::string("ultra"));
}

TEST("sayless-migrate: project caveman.off -> sayless.off") {
    auto root = tmpDir("proj-off");
    fs::create_directories(root / ".icmg");
    std::ofstream(root / ".icmg" / "caveman.off") << "off\n";
    int migrated = migrateCavemanToSayless(root);
    ASSERT_EQ(migrated, 1);
    ASSERT_TRUE(fs::exists(root / ".icmg" / "sayless.off"));
    ASSERT_FALSE(fs::exists(root / ".icmg" / "caveman.off"));
}

TEST("sayless-migrate: idempotent — second call returns 0") {
    auto root = tmpDir("idem");
    fs::create_directories(root / ".icmg");
    std::ofstream(root / ".icmg" / "caveman.flag") << "full\n";
    int first = migrateCavemanToSayless(root);
    int second = migrateCavemanToSayless(root);
    ASSERT_EQ(first, 1);
    ASSERT_EQ(second, 0);
}

TEST("sayless-migrate: skip when destination already exists (preserve current)") {
    auto root = tmpDir("collision");
    fs::create_directories(root / ".icmg");
    std::ofstream(root / ".icmg" / "caveman.flag") << "ultra\n";
    std::ofstream(root / ".icmg" / "sayless.flag") << "hyper\n";  // user already on new
    int migrated = migrateCavemanToSayless(root);
    ASSERT_EQ(migrated, 0);  // no clobber
    std::ifstream f(root / ".icmg" / "sayless.flag");
    std::string lvl; std::getline(f, lvl);
    ASSERT_EQ(lvl, std::string("hyper"));  // preserved
    ASSERT_TRUE(fs::exists(root / ".icmg" / "caveman.flag"));  // old left for user to delete
}
