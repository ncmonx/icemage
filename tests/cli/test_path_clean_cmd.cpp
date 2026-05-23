// v1.11.0 T1: path-clean command smoke tests.
//
// `icmg path-clean` strips dead-drive PATH entries to kill the B:/ popup at
// shell-PATH-lookup level. Win-only; POSIX no-op.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <iostream>
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

TEST("path-clean: registered") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("path-clean");
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->name(), std::string("path-clean"));
}

TEST("path-clean: empty args prints usage (Win) or POSIX no-op message") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("path-clean");
    StdoutCapture cap;
    int rc = cmd->run({});
    ASSERT_EQ(rc, 0);
#ifdef _WIN32
    ASSERT_CONTAINS(cap.str(), "Usage");
#else
    ASSERT_CONTAINS(cap.str(), "POSIX no-op");
#endif
}

TEST("path-clean --help mentions status/apply/--system") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("path-clean");
    StdoutCapture cap;
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
#ifdef _WIN32
    ASSERT_CONTAINS(cap.str(), "status");
    ASSERT_CONTAINS(cap.str(), "apply");
    ASSERT_CONTAINS(cap.str(), "--system");
#endif
}

#ifdef _WIN32
TEST("path-clean status: dry-run runs without crash") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("path-clean");
    StdoutCapture cap;
    int rc = cmd->run({"status"});
    ASSERT_EQ(rc, 0);
    // Always reports "Total dead-drive entries:".
    ASSERT_CONTAINS(cap.str(), "Total dead-drive entries:");
}

TEST("path-clean: unknown action returns 1") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("path-clean");
    StderrCapture err;
    int rc = cmd->run({"not-real"});
    ASSERT_EQ(rc, 1);
    ASSERT_CONTAINS(err.str(), "unknown action");
}
#else
TEST("path-clean: POSIX no-op returns 0") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("path-clean");
    StdoutCapture cap;
    int rc = cmd->run({"status"});
    ASSERT_EQ(rc, 0);
    ASSERT_CONTAINS(cap.str(), "POSIX no-op");
}
#endif


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
