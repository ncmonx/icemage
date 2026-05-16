// test_ingest_history_cmd — unit tests for `icmg ingest-history`.
// Tests must NOT make real network calls or depend on `gh` being installed.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace cli  = icmg::cli;
namespace core = icmg::core;

// ---- TEST 1: command registered ----------------------------------------

TEST("ingest_history_cmd: registered in command registry") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("ingest-history");
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->name(), std::string("ingest-history"));
}

// ---- TEST 2: --help prints usage ---------------------------------------

TEST("ingest_history_cmd: --help prints usage containing key strings") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("ingest-history");
    ASSERT_TRUE(static_cast<bool>(cmd));

    // Redirect stdout to capture usage output.
    std::streambuf* orig = std::cout.rdbuf();
    std::ostringstream captured;
    std::cout.rdbuf(captured.rdbuf());

    int rc = cmd->run({"--help"});

    std::cout.rdbuf(orig);
    std::string out = captured.str();

    ASSERT_EQ(rc, 0);
    ASSERT_CONTAINS(out, "ingest-history");
    ASSERT_CONTAINS(out, "--pr-count");
}

// ---- TEST 3: missing / broken gh tolerated (exits 0 with warning) ------

TEST("ingest_history_cmd: absent or broken gh exits 0 with warning to stderr") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("ingest-history");
    ASSERT_TRUE(static_cast<bool>(cmd));

    // Use a nonsensical gh override so the shell command will fail even if gh is
    // installed.  We rely on the command tolerating gh failure (exit_code != 0).
    // We cannot easily inject a fake binary here, but the command is required
    // to return 0 (not crash) when gh is absent or returns non-zero.
    //
    // Pass --pr-count 0 so even a working gh call ingests nothing, and a broken
    // one should still return 0.
    std::streambuf* orig_out = std::cout.rdbuf();
    std::streambuf* orig_err = std::cerr.rdbuf();
    std::ostringstream cap_out, cap_err;
    std::cout.rdbuf(cap_out.rdbuf());
    std::cerr.rdbuf(cap_err.rdbuf());

    // --gh-cmd lets tests inject a fake command; fall back to always-fail sentinel.
    int rc = cmd->run({"--pr-count", "1", "--gh-cmd", "this-binary-does-not-exist-icmg-test"});

    std::cout.rdbuf(orig_out);
    std::cerr.rdbuf(orig_err);

    // Must not crash and must return 0 when gh fails.
    ASSERT_EQ(rc, 0);
    // A warning should have been emitted to stderr.
    std::string err = cap_err.str();
    ASSERT_TRUE(!err.empty());
}

int main() {
    std::cout << "=== ingest_history_cmd tests ===\n";
    return icmg::test::run_all();
}
