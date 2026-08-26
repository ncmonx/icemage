// 2026-08-25 brain v2.22 #1: --as-of time parsing for time-travel recall.
// Pure, no IO. Accepts: unix epoch seconds ("1756100000"), relative duration
// meaning that long AGO ("7d" / "24h" / "30m"), or a date "YYYY-MM-DD"
// (midnight UTC). Returns 0 on unparseable input.
#pragma once
#include <cstdint>
#include <string>

namespace icmg::cli {

// Days since civil epoch 1970-01-01 (Howard Hinnant's algorithm, no tm/mktime
// so the result is UTC-stable across platforms and TZ settings).
inline int64_t daysFromCivil(int64_t y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);            // [0, 399]
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Parse --as-of. `now_epoch` is injectable for tests (0 = wall clock caller
// supplies; cmd layer passes real now).
inline int64_t parseAsOf(const std::string& raw, int64_t now_epoch) {
    if (raw.empty()) return 0;
    // YYYY-MM-DD
    if (raw.size() == 10 && raw[4] == '-' && raw[7] == '-') {
        try {
            int64_t y = std::stoll(raw.substr(0, 4));
            int     m = std::stoi(raw.substr(5, 2));
            int     d = std::stoi(raw.substr(8, 2));
            if (m < 1 || m > 12 || d < 1 || d > 31) return 0;
            return daysFromCivil(y, m, d) * 86400;
        } catch (...) { return 0; }
    }
    // Relative: <N>d / <N>h / <N>m == that long AGO
    char unit = raw.back();
    if (unit == 'd' || unit == 'h' || unit == 'm') {
        try {
            int64_t n = std::stoll(raw.substr(0, raw.size() - 1));
            if (n < 0) return 0;
            int64_t mul = (unit == 'd') ? 86400 : (unit == 'h') ? 3600 : 60;
            return now_epoch - n * mul;
        } catch (...) { return 0; }
    }
    // Plain epoch
    try {
        int64_t e = std::stoll(raw);
        return e > 0 ? e : 0;
    } catch (...) { return 0; }
}

} // namespace icmg::cli
