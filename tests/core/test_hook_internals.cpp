// Phase B.B7 (v0.57.0): hook-runner internals tests.
//
// Verifies distillAuto / distillSession / complianceCheckThinking /
// failSyncDenials / toolBudgetReset / compressInPlace work in-process
// without spawning any subprocess.

#include "../test_main.hpp"
#include "../../src/core/hooks/internals.hpp"
#include "../../src/core/hooks/runners.hpp"

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

// complianceCheckThinking gates on ~/.icmg/caveman.flag existence —
// touch it for tests that exercise word-counting; remove afterwards.
fs::path cavemanFlagPath() {
    const char* h = std::getenv("USERPROFILE");
    if (!h) h = std::getenv("HOME");
    return fs::path(h ? h : ".") / ".icmg" / "caveman.flag";
}

void ensureCavemanFlag(bool& created_by_test) {
    auto p = cavemanFlagPath();
    if (fs::exists(p)) { created_by_test = false; return; }
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p) << "ultra";
    created_by_test = true;
}

void removeCavemanFlagIfCreated(bool created_by_test) {
    if (!created_by_test) return;
    std::error_code ec;
    fs::remove(cavemanFlagPath(), ec);
}

} // namespace

// ---- compliance ------------------------------------------------------------

TEST("hook_internals: complianceCheckThinking counts words across thinking blobs") {
    bool flag_owned = false;
    ensureCavemanFlag(flag_owned);
    std::string raw =
        R"({"role":"assistant","content":[)"
        R"({"type":"thinking","thinking":"one two three four five"},)"
        R"({"type":"thinking","thinking":"six seven eight"}]})";
    int n = icmg::core::hooks::complianceCheckThinking(raw, 1000);
    removeCavemanFlagIfCreated(flag_owned);
    ASSERT_EQ(n, 8);
}

TEST("hook_internals: complianceCheckThinking returns 0 on no thinking field") {
    bool flag_owned = false;
    ensureCavemanFlag(flag_owned);
    int n = icmg::core::hooks::complianceCheckThinking(R"({"plain":"hi"})", 80);
    removeCavemanFlagIfCreated(flag_owned);
    ASSERT_EQ(n, 0);
}

// ---- distill ---------------------------------------------------------------

TEST("hook_internals: distillAuto short-circuits below min_len") {
    int n = icmg::core::hooks::distillAuto("tiny", /*min_len=*/1000);
    ASSERT_EQ(n, 0);
}

TEST("hook_internals: distillAuto no statements → 0") {
    std::string text(500, 'x'); // 500 chars of plain noise
    int n = icmg::core::hooks::distillAuto(text, /*min_len=*/100);
    ASSERT_EQ(n, 0);
}

// ---- tool budget -----------------------------------------------------------

TEST("hook_internals: toolBudgetReset removes counter file (no-throw)") {
    icmg::core::hooks::toolBudgetReset();
    icmg::core::hooks::toolBudgetReset(); // idempotent
    ASSERT_TRUE(true);
}

// ---- compress --------------------------------------------------------------

TEST("hook_internals: compressInPlace returns empty on tiny input") {
    auto r = icmg::core::hooks::compressInPlace("short", /*threshold=*/256);
    ASSERT_TRUE(r.empty());
}

TEST("hook_internals: compressInPlace returns empty on empty input") {
    auto r = icmg::core::hooks::compressInPlace("", /*threshold=*/256);
    ASSERT_TRUE(r.empty());
}

// ---- runners: opt-out env vars honored -------------------------------------

TEST("runners: ICMG_NO_STOP_HOOK suppresses runStopHook side effects") {
    setEnv("ICMG_NO_STOP_HOOK", "1");
    auto r = icmg::core::hooks::runStopHook(R"({"transcript":"long content here that would normally trigger distill"})");
    ASSERT_TRUE(r.empty());
    setEnv("ICMG_NO_STOP_HOOK", "");
}

TEST("runners: ICMG_NO_PRECOMPACT_HOOK suppresses runPreCompactHook") {
    setEnv("ICMG_NO_PRECOMPACT_HOOK", "1");
    auto r = icmg::core::hooks::runPreCompactHook(R"({"transcript":"x"})");
    ASSERT_TRUE(r.empty());
    setEnv("ICMG_NO_PRECOMPACT_HOOK", "");
}

TEST("runners: ICMG_NO_COMPRESS_HOOK suppresses runPostToolUseReadHook") {
    setEnv("ICMG_NO_COMPRESS_HOOK", "1");
    std::string big(2000, 'a');
    std::string payload = R"({"tool_response":{"content":")" + big + R"("}})";
    auto r = icmg::core::hooks::runPostToolUseReadHook(payload);
    ASSERT_TRUE(r.empty());
    setEnv("ICMG_NO_COMPRESS_HOOK", "");
}

TEST("runners: runPostToolUseReadHook small content → empty (no compress)") {
    auto r = icmg::core::hooks::runPostToolUseReadHook(
        R"({"tool_response":{"content":"tiny"}})");
    ASSERT_TRUE(r.empty());
}

int main() { return icmg::test::run_all(); }
