// test_hook_post_edit_sync — unit tests for runPostToolUseEditAutoSync (T3).
//
// Tests:
//   1. Empty input → empty output.
//   2. ICMG_AUTO_SYNC_QUIET=1 → empty output.
//   3. Valid Edit stdin parses without crash → empty output (fail-soft).
//   4. Valid Write stdin parses without crash → empty output (fail-soft).

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

// ---- TEST 1: empty input → empty output ------------------------------------

TEST("runPostToolUseEditAutoSync empty input → empty output") {
    std::string out = icmg::core::hooks::runPostToolUseEditAutoSync("");
    ASSERT_EQ(out, std::string(""));
}

// ---- TEST 2: ICMG_AUTO_SYNC_QUIET suppresses --------------------------------

TEST("runPostToolUseEditAutoSync ICMG_AUTO_SYNC_QUIET=1 suppresses") {
    setEnv("ICMG_AUTO_SYNC_QUIET", "1");

    std::string valid_stdin =
        R"({"tool_name":"Edit","tool_input":{"file_path":"src/foo.cpp","old_string":"a","new_string":"b"}})";
    std::string out = icmg::core::hooks::runPostToolUseEditAutoSync(valid_stdin);

    setEnv("ICMG_AUTO_SYNC_QUIET", "");

    ASSERT_EQ(out, std::string(""));
}

// ---- TEST 3: valid Edit stdin parses without crash -------------------------

TEST("runPostToolUseEditAutoSync valid Edit stdin parses without crash") {
    // Ensure quiet env is off.
    setEnv("ICMG_AUTO_SYNC_QUIET", "");

    std::string edit_stdin =
        R"({"tool_name":"Edit","tool_input":{"file_path":"src/foo.cpp","old_string":"a","new_string":"b"}})";

    // Must not throw; returns "" (fail-soft: DB may be unavailable in test env).
    std::string out = icmg::core::hooks::runPostToolUseEditAutoSync(edit_stdin);
    ASSERT_EQ(out, std::string(""));
}

// ---- TEST 4: valid Write stdin parses without crash ------------------------

TEST("runPostToolUseEditAutoSync valid Write stdin parses without crash") {
    setEnv("ICMG_AUTO_SYNC_QUIET", "");

    std::string write_stdin =
        R"({"tool_name":"Write","tool_input":{"file_path":"src/foo.cpp","content":"new content"}})";

    std::string out = icmg::core::hooks::runPostToolUseEditAutoSync(write_stdin);
    ASSERT_EQ(out, std::string(""));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
