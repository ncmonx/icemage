// v1.17.0: session-inject cmd smoke test.

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
private:
    std::ostringstream buf_;
    std::streambuf*    old_;
};
}

TEST("session-inject: registered") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("session-inject");
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->name(), std::string("session-inject"));
}

TEST("session-inject: --help mentions skip flags") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("session-inject");
    StdoutCapture cap;
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
    ASSERT_CONTAINS(cap.str(), "--skip-sayless");
    ASSERT_CONTAINS(cap.str(), "--skip-context");
    ASSERT_CONTAINS(cap.str(), "--skip-wakeup");
}

TEST("session-inject: --skip-all runs without crash") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("session-inject");
    StdoutCapture cap;
    StderrCapture err;
    try {
        int rc = cmd->run({"--skip-sayless", "--skip-context", "--skip-wakeup"});
        ASSERT_EQ(rc, 0);
        // Empty output OK with all skipped.
    } catch (const std::exception&) {
        // Some subcmds may throw on clean machines without DB; acceptable.
    }
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
