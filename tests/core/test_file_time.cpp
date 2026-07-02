// Issue #222 ROOT FIX: MSVC clock_cast<system_clock>(file_clock) routes through
// utc_clock -> get_tzdb_list() -> LoadLibrary("icu.dll") -> throws
// system_error(126) on SKUs without ICU (Windows Server 2019 / Core). The
// stale-check only compares mtimes at 5s tolerance, so the leap-second-aware
// conversion is unnecessary: pure FILETIME-epoch arithmetic (never throws, no
// DLL) replaces it. This pure helper is the single conversion used by both
// bundle_cmd (context) and graph_cmd (graph update).

#include "../test_main.hpp"
#include "../../src/core/file_time.hpp"

using namespace icmg::core;

TEST("file_time: Windows file_clock seconds (epoch 1601) -> unix seconds") {
    // 2026-07-02 ~= unix 1_782_000_000; file-clock secs = unix + 11644473600
    ASSERT_EQ(unixFromFileClockSeconds(1782000000LL + 11644473600LL),
              (long long)1782000000LL);
}

TEST("file_time: already-unix-scale value passes through (libstdc++ delta=0)") {
    ASSERT_EQ(unixFromFileClockSeconds(1782000000LL), (long long)1782000000LL);
}

TEST("file_time: boundary — value == delta passes through (strict >, matches v1.53)") {
    // The conversion subtracts only when raw > delta (faithful to the original
    // fallback). raw == delta is ambiguous (unix year 2338 vs file-clock 1970)
    // and never occurs for real mtimes, so it passes through unchanged — this
    // pins the exact boundary behavior of the code being refactored.
    ASSERT_EQ(unixFromFileClockSeconds(11644473600LL), (long long)11644473600LL);
}

TEST("file_time: one second past delta subtracts -> unix 1") {
    ASSERT_EQ(unixFromFileClockSeconds(11644473601LL), (long long)1LL);
}

TEST("file_time: below-delta value (odd epoch, e.g. MinGW 2174) untouched") {
    ASSERT_EQ(unixFromFileClockSeconds(-42LL), (long long)-42LL);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
