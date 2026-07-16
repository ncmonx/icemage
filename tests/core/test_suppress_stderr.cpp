#include "../test_main.hpp"
#include "../../src/core/exec_utils.hpp"
#include <string>

using icmg::core::suppressStderr;

// Regression guard for the 2026-07-16 stray-`nul` class of bug: any command
// routed through safeExecShell() (which prefers bash even on Windows) must NOT
// carry a Windows `nul` redirect target -- under bash that creates a literal
// file named `nul`. suppressStderr() is the sanctioned way to silence stderr,
// and it must always emit the bash-safe /dev/null form.
TEST("suppressStderr: appends the bash-safe /dev/null redirect") {
    ASSERT_EQ(suppressStderr("schtasks /Query /TN x"),
              std::string("schtasks /Query /TN x 2>/dev/null"));
}

TEST("suppressStderr: never emits a bare Windows `nul` redirect target") {
    auto out = suppressStderr("taskkill /PID 123 /F");
    ASSERT_TRUE(out.find("2>nul") == std::string::npos);
    ASSERT_TRUE(out.find(">nul") == std::string::npos);
    ASSERT_TRUE(out.find("/dev/null") != std::string::npos);
}

TEST("suppressStderr: is a pure suffix (original command preserved verbatim)") {
    const std::string base = "MSYS_NO_PATHCONV=1 schtasks /Query /FO LIST";
    auto out = suppressStderr(base);
    ASSERT_EQ(out.substr(0, base.size()), base);
}
