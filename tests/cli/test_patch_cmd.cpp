// test_patch_cmd — unit tests for `icmg patch` (unified diff application).
//
// Tests:
//   1. patch: simple single-hunk apply succeeds
//   2. patch: multi-hunk apply succeeds
//   3. patch: file-not-found returns rc=3
//   4. patch: malformed diff (no @@ header) returns rc=1
//   5. patch: context-mismatch hunk returns rc=2
//   6. patch: --dry-run does not modify file
//   7. patch: strip prefix correctly maps path
//   8. patch: --help returns 0
#include "../test_main.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/registry.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---- helpers ---------------------------------------------------------------

static fs::path writeTmp(const std::string& name, const std::string& content) {
    fs::path p = fs::temp_directory_path() / name;
    std::ofstream f(p, std::ios::trunc);
    f << content;
    return p;
}

static std::string readTmp(const fs::path& p) {
    std::ifstream f(p);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Suppress stdout/stderr while running the command; return captured output.
struct SilentRun {
    std::string out, err;
    int rc = -1;

    SilentRun(icmg::cli::BaseCommand* cmd, std::vector<std::string> args) {
        std::streambuf* old_out = std::cout.rdbuf();
        std::streambuf* old_err = std::cerr.rdbuf();
        std::ostringstream so, se;
        std::cout.rdbuf(so.rdbuf());
        std::cerr.rdbuf(se.rdbuf());
        rc = cmd->run(args);
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        out = so.str();
        err = se.str();
    }
};

// ---------------------------------------------------------------------------
// TEST 1 — simple single-hunk apply
// ---------------------------------------------------------------------------
TEST("patch: simple single-hunk apply succeeds") {
    // Create target file.
    fs::path target = writeTmp("patch_simple.txt",
        "line one\n"
        "line two\n"
        "line three\n");

    // Diff that replaces "line two" with "line TWO".
    // The path in the diff doesn't matter when we pass the target explicitly.
    std::string diff =
        "--- a/dummy.txt\n"
        "+++ b/dummy.txt\n"
        "@@ -1,3 +1,3 @@\n"
        " line one\n"
        "-line two\n"
        "+line TWO\n"
        " line three\n";

    auto cmd = ::icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("patch");
    ASSERT_TRUE(cmd != nullptr);

    SilentRun r(cmd.get(), {target.string(), "--diff", diff, "--strip", "0"});
    ASSERT_EQ(r.rc, 0);

    std::string result = readTmp(target);
    ASSERT_CONTAINS(result, "line TWO");
    ASSERT_NOT_CONTAINS(result, "line two\n");
    fs::remove(target);
}

// ---------------------------------------------------------------------------
// TEST 2 — multi-hunk apply
// ---------------------------------------------------------------------------
TEST("patch: multi-hunk apply succeeds") {
    fs::path target = writeTmp("patch_multi.txt",
        "alpha\n"
        "beta\n"
        "gamma\n"
        "delta\n"
        "epsilon\n");

    // Two hunks: replace "beta" with "BETA", and "delta" with "DELTA".
    std::string diff =
        "--- a/multi.txt\n"
        "+++ b/multi.txt\n"
        "@@ -1,3 +1,3 @@\n"
        " alpha\n"
        "-beta\n"
        "+BETA\n"
        " gamma\n"
        "@@ -3,3 +3,3 @@\n"
        " gamma\n"
        "-delta\n"
        "+DELTA\n"
        " epsilon\n";

    auto cmd = ::icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("patch");
    ASSERT_TRUE(cmd != nullptr);

    SilentRun r(cmd.get(), {target.string(), "--diff", diff, "--strip", "0"});
    ASSERT_EQ(r.rc, 0);

    std::string result = readTmp(target);
    ASSERT_CONTAINS(result, "BETA");
    ASSERT_CONTAINS(result, "DELTA");
    ASSERT_NOT_CONTAINS(result, "beta\n");
    ASSERT_NOT_CONTAINS(result, "delta\n");
    fs::remove(target);
}

// ---------------------------------------------------------------------------
// TEST 3 — file-not-found returns rc=3
// ---------------------------------------------------------------------------
TEST("patch: file-not-found returns rc=3") {
    // A valid-looking diff, but the target file does not exist.
    std::string diff =
        "--- a/nosuchfile.txt\n"
        "+++ b/nosuchfile.txt\n"
        "@@ -1,2 +1,2 @@\n"
        " context\n"
        "-old\n"
        "+new\n";

    auto cmd = ::icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("patch");
    ASSERT_TRUE(cmd != nullptr);

    // Supply a path that definitely does not exist.
    std::string ghost = (fs::temp_directory_path() / "icmg_patch_nosuchfile_xyz.txt").string();
    fs::remove(ghost);  // ensure it's gone

    SilentRun r(cmd.get(), {ghost, "--diff", diff, "--strip", "0"});
    // rc=3: I/O error (file not found).
    ASSERT_EQ(r.rc, 3);
}

// ---------------------------------------------------------------------------
// TEST 4 — malformed diff (no valid hunks) returns rc=1
// ---------------------------------------------------------------------------
TEST("patch: malformed diff with no hunks returns rc=1") {
    fs::path target = writeTmp("patch_malformed.txt", "hello\n");

    // A diff string that has no @@ header at all — not parseable as any hunk.
    std::string diff = "this is not a unified diff at all\nno @@ markers\n";

    auto cmd = ::icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("patch");
    ASSERT_TRUE(cmd != nullptr);

    SilentRun r(cmd.get(), {target.string(), "--diff", diff});
    // Expect rc=1 (no valid hunks found).
    ASSERT_EQ(r.rc, 1);

    fs::remove(target);
}

// ---------------------------------------------------------------------------
// TEST 5 — context mismatch in hunk returns rc=2
// ---------------------------------------------------------------------------
TEST("patch: context mismatch returns rc=2") {
    // File content does NOT match what the diff's context lines expect.
    fs::path target = writeTmp("patch_mismatch.txt",
        "something\n"
        "completely\n"
        "different\n");

    // Diff expects "line one / line two / line three" which isn't there.
    std::string diff =
        "--- a/mismatch.txt\n"
        "+++ b/mismatch.txt\n"
        "@@ -1,3 +1,3 @@\n"
        " line one\n"
        "-line two\n"
        "+line TWO\n"
        " line three\n";

    auto cmd = ::icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("patch");
    ASSERT_TRUE(cmd != nullptr);

    SilentRun r(cmd.get(), {target.string(), "--diff", diff, "--strip", "0"});
    ASSERT_EQ(r.rc, 2);

    // File should be unchanged.
    std::string result = readTmp(target);
    ASSERT_CONTAINS(result, "something");
    fs::remove(target);
}

// ---------------------------------------------------------------------------
// TEST 6 — --dry-run does not modify file
// ---------------------------------------------------------------------------
TEST("patch: --dry-run leaves file unmodified") {
    const std::string original = "keep me\noriginal content\n";
    fs::path target = writeTmp("patch_dryrun.txt", original);

    std::string diff =
        "--- a/dry.txt\n"
        "+++ b/dry.txt\n"
        "@@ -1,2 +1,2 @@\n"
        " keep me\n"
        "-original content\n"
        "+replaced content\n";

    auto cmd = ::icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("patch");
    ASSERT_TRUE(cmd != nullptr);

    SilentRun r(cmd.get(), {target.string(), "--diff", diff, "--strip", "0", "--dry-run"});
    ASSERT_EQ(r.rc, 0);

    // File must remain untouched.
    std::string result = readTmp(target);
    ASSERT_EQ(result, original);
    ASSERT_NOT_CONTAINS(result, "replaced content");
    fs::remove(target);
}

// ---------------------------------------------------------------------------
// TEST 7 — strip prefix maps diff path to real file
// ---------------------------------------------------------------------------
TEST("patch: strip=1 strips one leading path component") {
    // Write the target at a flat name.
    fs::path target = writeTmp("patch_strip.txt",
        "foo\n"
        "bar\n"
        "baz\n");

    // Diff uses "a/<name>" / "b/<name>"; with --strip 1 the a/ and b/ are
    // removed, leaving the bare name which we redirect via the positional arg.
    std::string diff =
        "--- a/patch_strip.txt\n"
        "+++ b/patch_strip.txt\n"
        "@@ -1,3 +1,3 @@\n"
        " foo\n"
        "-bar\n"
        "+BAR\n"
        " baz\n";

    auto cmd = ::icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("patch");
    ASSERT_TRUE(cmd != nullptr);

    // Pass the real path as positional arg; strip=1 (default).
    SilentRun r(cmd.get(), {target.string(), "--diff", diff});
    ASSERT_EQ(r.rc, 0);

    std::string result = readTmp(target);
    ASSERT_CONTAINS(result, "BAR");
    ASSERT_NOT_CONTAINS(result, "bar\n");
    fs::remove(target);
}

// ---------------------------------------------------------------------------
// TEST 8 — --help returns 0
// ---------------------------------------------------------------------------
TEST("patch: --help returns 0") {
    auto cmd = ::icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("patch");
    ASSERT_TRUE(cmd != nullptr);

    SilentRun r(cmd.get(), {"--help"});
    ASSERT_EQ(r.rc, 0);
    // Help output should mention the command.
    ASSERT_CONTAINS(r.out, "patch");
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
