// v1.1.0 Task 6.5: service command + ServiceLoop contract test.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/core/service_loop.hpp"
#include "../../src/cli/base_command.hpp"

#include <memory>
#include <string>

namespace cli = icmg::cli;
namespace core = icmg::core;

TEST("service cmd registered") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("service");
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->name(), std::string("service"));
}

TEST("service status: not-running short-circuits cleanly") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("service");
    ASSERT_TRUE(static_cast<bool>(cmd));
    int rc = cmd->run({"status"});
    ASSERT_EQ(rc, 0);
}

TEST("service state + pid paths are non-empty") {
    auto sp = core::serviceStatePath();
    auto pp = core::servicePidPath();
    ASSERT_TRUE(!sp.empty());
    ASSERT_TRUE(!pp.empty());
}

TEST("service requestStop / shouldStop roundtrip") {
    bool before = core::ServiceLoop::shouldStop();
    core::ServiceLoop::requestStop();
    bool after  = core::ServiceLoop::shouldStop();
    ASSERT_TRUE(!before);
    ASSERT_TRUE(after);
}

int main() { return icmg::test::run_all(); }
