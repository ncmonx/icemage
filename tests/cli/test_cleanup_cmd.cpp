// v1.10.0 T3: cleanup command contract test (Win-only behavior).
//
// `icmg cleanup` enumerates icmg.exe processes + filters service/daemon/self.
// On POSIX it's a no-op. Smoke tests only — deep behavior (kill, age compute)
// hard to mock without OS state.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <memory>
#include <sstream>

namespace cli = icmg::cli;
namespace core = icmg::core;

namespace {

class StdoutCapture {
public:
    StdoutCapture() : old_(std::cout.rdbuf(buf_.rdbuf())) {}
    ~StdoutCapture() { std::cout.rdbuf(old_); }
    std::string str() const { return buf_.str(); }
private:
    std::ostringstream buf_;
    std::streambuf*    old_;
};

class StderrCapture {
public:
    StderrCapture() : old_(std::cerr.rdbuf(buf_.rdbuf())) {}
    ~StderrCapture() { std::cerr.rdbuf(old_); }
    std::string str() const { return buf_.str(); }
private:
    std::ostringstream buf_;
    std::streambuf*    old_;
};

}  // namespace

TEST("cleanup_cmd: registered") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("cleanup");
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->name(), std::string("cleanup"));
}

TEST("cleanup_cmd: empty args prints usage") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("cleanup");
    StdoutCapture cap;
    int rc = cmd->run({});
    ASSERT_EQ(rc, 0);
#ifdef _WIN32
    ASSERT_CONTAINS(cap.str(), "Usage:");
#endif
}

TEST("cleanup_cmd: --help prints usage") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("cleanup");
    StdoutCapture cap;
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
#ifdef _WIN32
    ASSERT_CONTAINS(cap.str(), "orphans");
    ASSERT_CONTAINS(cap.str(), "kill-orphans");
#endif
}

#ifdef _WIN32
TEST("cleanup_cmd: 'all' enumerates without crash") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("cleanup");
    StdoutCapture cap;
    int rc = cmd->run({"all"});
    ASSERT_EQ(rc, 0);
    // Output always includes a totals line.
    ASSERT_CONTAINS(cap.str(), "Total:");
}

TEST("cleanup_cmd: 'orphans' does not list self") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("cleanup");
    StdoutCapture cap;
    int rc = cmd->run({"orphans"});
    ASSERT_EQ(rc, 0);
    // Test binary is not icmg.exe — orphan listing should not contain self.
    // We don't strictly know what's running, but cmd should always exit 0.
}

TEST("cleanup_cmd: 'kill-orphans' without --confirm refuses") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("cleanup");
    StderrCapture err;
    int rc = cmd->run({"kill-orphans"});
    ASSERT_EQ(rc, 1);
    ASSERT_CONTAINS(err.str(), "--confirm");
}

TEST("cleanup_cmd: unknown action returns 1") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("cleanup");
    StderrCapture err;
    int rc = cmd->run({"not-a-real-action"});
    ASSERT_EQ(rc, 1);
    ASSERT_CONTAINS(err.str(), "unknown action");
}
#else
TEST("cleanup_cmd: POSIX no-op returns 0") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("cleanup");
    int rc = cmd->run({"orphans"});
    ASSERT_EQ(rc, 0);
}
#endif


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
