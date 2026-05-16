// test_hook_test_fail_context — unit tests for runPostToolUseTestFailContext (T13).
//
// Tests:
//   1. Empty output → empty string.
//   2. Non-failure output → empty string.
//   3. FAIL signature triggers block (contains "## Debug context", path extracted).
//   4. Traceback signature triggers block (contains .py file path).
//   5. ICMG_DEBUG_CONTEXT_QUIET=1 → empty string.

#include "../test_main.hpp"
#include "../../src/core/hooks/internals.hpp"

#include <cstdlib>
#include <string>

namespace {

void setEnv(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v ? v : "");
#else
    if (v && *v) setenv(k, v, 1); else unsetenv(k);
#endif
}

} // namespace

// ---- TEST 1: empty output → empty ------------------------------------------

TEST("runPostToolUseTestFailContext empty output → empty") {
    std::string out = icmg::core::hooks::runPostToolUseTestFailContext("", "");
    ASSERT_EQ(out, std::string(""));
}

// ---- TEST 2: non-failure output → empty ------------------------------------

TEST("runPostToolUseTestFailContext non-failure output → empty") {
    std::string normal_output =
        "Build complete.\n"
        "Linking icmg.exe ...\n"
        "cmake build finished in 12s\n"
        "All objects compiled.\n";
    std::string out = icmg::core::hooks::runPostToolUseTestFailContext(
        "cmake --build build", normal_output);
    ASSERT_EQ(out, std::string(""));
}

// ---- TEST 3: FAIL signature triggers block ---------------------------------

TEST("runPostToolUseTestFailContext FAIL signature triggers block") {
    std::string fail_output =
        "Running tests...\n"
        "FAIL: tests/test_foo.cpp:42 assertion failed: expected 1 got 2\n"
        "  in function check_result\n"
        "1 failed, 9 passed\n";
    std::string out = icmg::core::hooks::runPostToolUseTestFailContext(
        "ctest --test-dir build", fail_output);
    ASSERT_TRUE(!out.empty());
    ASSERT_CONTAINS(out, "## Debug context");
    ASSERT_CONTAINS(out, "test_foo.cpp");
}

// ---- TEST 4: Traceback signature triggers block ----------------------------

TEST("runPostToolUseTestFailContext Traceback triggers block") {
    std::string traceback_output =
        "Traceback (most recent call last):\n"
        "  File \"src/scripts/process.py\", line 87, in run\n"
        "    result = parse(data)\n"
        "ValueError: invalid literal for int\n";
    std::string out = icmg::core::hooks::runPostToolUseTestFailContext(
        "python src/scripts/process.py", traceback_output);
    ASSERT_TRUE(!out.empty());
    ASSERT_CONTAINS(out, "## Debug context");
    ASSERT_CONTAINS(out, "process.py");
}

// ---- TEST 5: ICMG_DEBUG_CONTEXT_QUIET=1 → empty ---------------------------

TEST("runPostToolUseTestFailContext ICMG_DEBUG_CONTEXT_QUIET=1 suppresses") {
    setEnv("ICMG_DEBUG_CONTEXT_QUIET", "1");

    std::string fail_output =
        "FAIL: tests/test_bar.cpp:10 something went wrong\n"
        "error: build stopped\n";
    std::string out = icmg::core::hooks::runPostToolUseTestFailContext(
        "ctest --test-dir build", fail_output);

    setEnv("ICMG_DEBUG_CONTEXT_QUIET", "");

    ASSERT_EQ(out, std::string(""));
}

int main() {
    std::cout << "=== hook_test_fail_context tests ===\n";
    return icmg::test::run_all();
}
