// v1.27.0 (Phase 5): Coverage backfill — parametric smoke test over all
// registered CLI commands. Closes ~100 untested cmds with one TEST().
//
// Asserts every cmd registered via ICMG_REGISTER_COMMAND has:
//   - non-empty name() matching the registry key
//   - non-empty description()
//   - construct without throw

#include "../test_main.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/registry.hpp"

#include <string>
#include <vector>

using icmg::cli::BaseCommand;
using icmg::core::Registry;

TEST("command_registry: at least 50 commands registered") {
    auto keys = Registry<BaseCommand>::instance().keys();
    ASSERT_TRUE((int)keys.size() >= 50);
}

TEST("command_registry: every cmd constructs + name matches key + non-empty desc") {
    auto& reg = Registry<BaseCommand>::instance();
    auto keys = reg.keys();
    int checked = 0;
    int failed = 0;
    for (const auto& k : keys) {
        try {
            auto cmd = reg.create(k);
            if (!cmd) { ++failed; continue; }
            std::string n = cmd->name();
            std::string d = cmd->description();
            if (n.empty() || d.empty() || n != k) ++failed;
            ++checked;
        } catch (...) {
            ++failed;
        }
    }
    ASSERT_TRUE(checked > 0);
    ASSERT_EQ(failed, 0);
}

TEST("command_registry: critical core cmds present") {
    auto& reg = Registry<BaseCommand>::instance();
    // Spot-check the most heavily used commands across the dispatcher.
    const std::vector<std::string> must_have = {
        "init", "doctor", "context", "graph", "recall", "store",
        "run", "pack", "verify", "parallel", "memory", "savings",
        "compress", "expand", "shrink", "fetch", "session",
        "port",        // v1.24.0
        "fail",        // anti-pattern
        "known-issue", // bug recall
        "memoir",      // post-mortems
        "wflog",       // session boundary
    };
    for (const auto& k : must_have) {
        bool present = reg.has(k);
        ASSERT_TRUE(present);
    }
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
