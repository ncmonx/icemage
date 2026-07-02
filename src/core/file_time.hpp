#pragma once
// Issue #222 ROOT FIX — file_clock -> Unix seconds WITHOUT std::chrono::clock_cast.
//
// MSVC's clock_cast<system_clock>(file_time) converts through utc_clock to be
// leap-second-correct, which calls std::chrono::get_tzdb_list() -> the MSVC STL
// lazily LoadLibrary's icu.dll. On Windows SKUs without ICU (Server 2019 /
// Server Core) that load fails and the STL throws std::system_error(126,
// "The specified module could not be found") — crashing `icmg context` and
// `icmg graph update` (the only stale-check paths). Diagnosed via throw_site
// capture on the affected host: get_tzdb_list <- get_leap_second_info <-
// clock_time_conversion <- clock_cast <- ContextCommand::run.
//
// Staleness compares mtimes at 5s tolerance, so leap-second correctness is
// irrelevant. Pure epoch-delta arithmetic: never throws, loads nothing.
// Windows FILETIME epoch = 1601-01-01; Unix = 1970-01-01; delta 11644473600 s.
// libstdc++ file_clock delta = 0 (values pass through). Odd epochs (MinGW
// 2174) yield values below the delta and also pass through — matching the
// prior fallback branch behavior.
#include <cstdint>

namespace icmg::core {

constexpr std::int64_t kFiletimeUnixDeltaSec = 11644473600LL;

// Raw seconds since the platform file_clock epoch -> Unix seconds.
inline long long unixFromFileClockSeconds(std::int64_t raw) {
    return (raw > kFiletimeUnixDeltaSec) ? (raw - kFiletimeUnixDeltaSec) : raw;
}

}  // namespace icmg::core
