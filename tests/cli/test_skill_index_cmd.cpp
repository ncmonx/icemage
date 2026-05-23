// v1.1.0 Task 5: skill-index alias registration test.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <memory>

namespace cli = icmg::cli;
namespace core = icmg::core;

TEST("skill-index alias registered") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("skill-index");
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->name(), std::string("skill-index"));
}

TEST("skill index nested still registered") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
