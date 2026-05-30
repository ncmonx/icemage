// v1.1.0 Task 6 + 6.6 — PreToolUse hard-deny + sayless re-inject tests.

#include "../test_main.hpp"
#include "../../src/core/hooks/internals.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {
void setEnv(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v ? v : "");
#else
    if (v && *v) setenv(k, v, 1); else unsetenv(k);
#endif
}
} // namespace

// ---- PreToolUse hard-deny --------------------------------------------------

TEST("enforce: ICMG_STRICT_BYPASS=1 → always allow") {
    setEnv("ICMG_STRICT_BYPASS", "1");
    auto r = icmg::core::hooks::runPreToolUseEnforce(
        R"({"tool_name":"Bash","tool_input":{"command":"cat anything.cpp"}})");
    ASSERT_TRUE(r.find("\"allow\"") != std::string::npos);
    setEnv("ICMG_STRICT_BYPASS", "");
}

TEST("enforce: Bash 'cat foo.cpp' → deny with icmg context hint") {
    auto r = icmg::core::hooks::runPreToolUseEnforce(
        R"({"tool_name":"Bash","tool_input":{"command":"cat src/main.cpp"}})");
    ASSERT_TRUE(r.find("\"deny\"") != std::string::npos);
    ASSERT_TRUE(r.find("icmg context") != std::string::npos);
}

TEST("enforce: Bash 'icmg recall x' → allow (icmg's own invocation)") {
    auto r = icmg::core::hooks::runPreToolUseEnforce(
        R"({"tool_name":"Bash","tool_input":{"command":"icmg recall foo"}})");
    ASSERT_TRUE(r.find("\"allow\"") != std::string::npos);
}

TEST("enforce: Bash 'powershell.exe -Command ...' → deny") {
    auto r = icmg::core::hooks::runPreToolUseEnforce(
        R"({"tool_name":"Bash","tool_input":{"command":"powershell.exe -Command Get-Process"}})");
    ASSERT_TRUE(r.find("\"deny\"") != std::string::npos);
}

TEST("enforce: Bash 'grep -r foo .' → deny") {
    auto r = icmg::core::hooks::runPreToolUseEnforce(
        R"({"tool_name":"Bash","tool_input":{"command":"grep -r foo ."}})");
    ASSERT_TRUE(r.find("\"deny\"") != std::string::npos);
}

TEST("enforce: empty stdin → allow (fail-open)") {
    auto r = icmg::core::hooks::runPreToolUseEnforce("");
    ASSERT_TRUE(r.find("\"allow\"") != std::string::npos);
}

TEST("enforce: unrelated tool (Write) → allow") {
    auto r = icmg::core::hooks::runPreToolUseEnforce(
        R"({"tool_name":"Write","tool_input":{"file_path":"x","content":"y"}})");
    ASSERT_TRUE(r.find("\"allow\"") != std::string::npos);
}

// ---- sayless re-inject -----------------------------------------------------

namespace {
fs::path saylessFlag() {
    const char* h = std::getenv("USERPROFILE");
    if (!h) h = std::getenv("HOME");
    return fs::path(h ? h : ".") / ".icmg" / "sayless.flag";
}
} // namespace

TEST("sayless_inject: no flag → empty block") {
    auto p = saylessFlag();
    bool existed = fs::exists(p);
    if (existed) {
        // Test must run with flag absent; rename, test, restore.
        std::error_code ec;
        fs::rename(p, p.string() + ".bak", ec);
    }
    auto r = icmg::core::hooks::runUserPromptSaylessInject();
    if (existed) {
        std::error_code ec;
        fs::rename(p.string() + ".bak", p, ec);
    }
    ASSERT_TRUE(r.empty());
}

TEST("sayless_inject: ICMG_SAYLESS_QUIET=1 → empty block") {
    setEnv("ICMG_SAYLESS_QUIET", "1");
    auto r = icmg::core::hooks::runUserPromptSaylessInject();
    ASSERT_TRUE(r.empty());
    setEnv("ICMG_SAYLESS_QUIET", "");
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
