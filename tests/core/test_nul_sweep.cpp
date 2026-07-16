#include "../test_main.hpp"
#include "../../src/core/nul_sweep.hpp"
#include <string>
#include <fstream>
#include <filesystem>
#include <system_error>

using icmg::core::isStrayNulName;
using icmg::core::sweepStrayNulFiles;
using icmg::core::removeStrayNul;
namespace fs = std::filesystem;

TEST("isStrayNulName: exact lowercase nul is a stray artifact") {
    ASSERT_TRUE(isStrayNulName("nul"));
}

TEST("isStrayNulName: case-insensitive (NUL, Nul, nUl)") {
    ASSERT_TRUE(isStrayNulName("NUL"));
    ASSERT_TRUE(isStrayNulName("Nul"));
    ASSERT_TRUE(isStrayNulName("nUl"));
}

TEST("isStrayNulName: names that merely contain or extend 'nul' are NOT matched") {
    ASSERT_TRUE(!isStrayNulName("null"));
    ASSERT_TRUE(!isStrayNulName("nul.txt"));
    ASSERT_TRUE(!isStrayNulName("annul"));
    ASSERT_TRUE(!isStrayNulName("nula"));
    ASSERT_TRUE(!isStrayNulName(""));
}

TEST("isStrayNulName: unrelated filenames are not matched") {
    ASSERT_TRUE(!isStrayNulName("readme"));
    ASSERT_TRUE(!isStrayNulName("con"));   // another reserved name, but not nul
    ASSERT_TRUE(!isStrayNulName("n"));
}

// --- sweep behavior (temp dir) ---

// A unique scratch dir under the OS temp root for this test process.
static fs::path makeScratch(const std::string& tag) {
    std::error_code ec;
    fs::path d = fs::temp_directory_path(ec) /
                 ("icmg_nulsweep_" + tag + "_" +
                  std::to_string(static_cast<unsigned long long>(
                      reinterpret_cast<std::uintptr_t>(&tag))));
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

TEST("sweepStrayNulFiles: missing directory yields no hits and does not throw") {
    fs::path nope = fs::temp_directory_path() / "icmg_nulsweep_does_not_exist_xyz";
    std::error_code ec; fs::remove_all(nope, ec);
    auto hits = sweepStrayNulFiles({nope}, /*remove=*/false);
    ASSERT_TRUE(hits.empty());
}

TEST("sweepStrayNulFiles: non-nul files are never swept") {
    fs::path d = makeScratch("keep");
    for (const char* n : {"readme", "null", "nul.txt", "annul"}) {
        std::ofstream(d / n) << "x";
    }
    auto hits = sweepStrayNulFiles({d}, /*remove=*/true);
    ASSERT_TRUE(hits.empty());
    // all decoy files survive
    std::error_code ec;
    ASSERT_TRUE(fs::exists(d / "readme", ec));
    ASSERT_TRUE(fs::exists(d / "null", ec));
    ASSERT_TRUE(fs::exists(d / "nul.txt", ec));
    fs::remove_all(d, ec);
}

TEST("sweepStrayNulFiles: duplicate input dirs are swept once") {
    fs::path d = makeScratch("dedup");
    std::ofstream(d / "readme") << "x";
    auto hits = sweepStrayNulFiles({d, d}, /*remove=*/false);
    ASSERT_TRUE(hits.empty());
    std::error_code ec; fs::remove_all(d, ec);
}

#ifndef _WIN32
// A literal file named `nul` can only exist on a POSIX filesystem; Win32 maps
// the name to the null device and refuses to create it under a normal open.
// So the positive create+remove round-trip is exercised on Linux/macOS runners
// (where the bug's leftovers actually land when synced off a Windows box). The
// Windows remove path (\\?\ + _wremove) is smoke-tested via `icmg doctor`.
TEST("sweepStrayNulFiles: a real 'nul' file is found and (remove) deleted") {
    fs::path d = makeScratch("hit");
    std::ofstream(d / "nul") << "stray";
    std::ofstream(d / "keep") << "x";
    std::error_code ec;
    ASSERT_TRUE(fs::exists(d / "nul", ec));

    // dry pass: found but not removed
    auto found = sweepStrayNulFiles({d}, /*remove=*/false);
    ASSERT_EQ(found.size(), (size_t)1);
    ASSERT_CONTAINS(found[0], "nul");
    ASSERT_TRUE(fs::exists(d / "nul", ec));

    // remove pass: found and deleted; decoy survives
    auto removed = sweepStrayNulFiles({d}, /*remove=*/true);
    ASSERT_EQ(removed.size(), (size_t)1);
    ASSERT_TRUE(!fs::exists(d / "nul", ec));
    ASSERT_TRUE(fs::exists(d / "keep", ec));
    fs::remove_all(d, ec);
}

TEST("removeStrayNul: deleting an absent file is not an error (idempotent)") {
    fs::path d = makeScratch("absent");
    // No file created; remove should report success (nothing to remove).
    ASSERT_TRUE(removeStrayNul(d / "nul"));
    std::error_code ec; fs::remove_all(d, ec);
}
#endif
