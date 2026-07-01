#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace icmg::cli {

struct BadgeData {
    double    pct          = 0.0;
    long long saved_tokens = 0;
    double    cost_saved   = 0.0;
    long long total_calls  = 0;
};

// pct -> "99.3%" style; helper for 1-decimal.
inline std::string oneDecimal(double v) {
    char buf[32]; std::snprintf(buf, sizeof(buf), "%.1f", v); return std::string(buf);
}

inline const char* savingsColor(double pct) {
    if (pct >= 90) return "brightgreen";
    if (pct >= 70) return "green";
    if (pct >= 50) return "yellow";
    if (pct >= 30) return "orange";
    return "red";
}

inline std::string humanizeTokens(long long n) {
    char buf[32];
    if (n >= 1000000) { std::snprintf(buf, sizeof(buf), "%.1fM", n / 1000000.0); return buf; }
    if (n >= 1000)    { std::snprintf(buf, sizeof(buf), "%.0fk", n / 1000.0);    return buf; }
    return std::to_string(n);
}

inline const char* tokensColor(long long saved) {
    if (saved >= 1000000) return "brightgreen";
    if (saved >= 100000)  return "green";
    if (saved >= 10000)   return "yellow";
    return "orange";
}

inline const char* costColor(double usd) {
    if (usd >= 20) return "brightgreen";
    if (usd >= 5)  return "green";
    if (usd >= 1)  return "yellow";
    return "orange";
}

inline std::string endpointJson(const std::string& label,
                                const std::string& message,
                                const std::string& color) {
    return "{\"schemaVersion\":1,\"label\":\"" + label +
           "\",\"message\":\"" + message +
           "\",\"color\":\"" + color + "\"}";
}

// Build the argv for the `icmg savings --json` subprocess. Pure/testable.
// REGRESSION GUARD: `savings` reads its window via --window (NOT --window-days;
// flagValue is exact-match). badge's PUBLIC flag is --window-days, but it MUST
// translate to --window here or the window is silently ignored -> wrong (default
// 30-day) data in an advertised flag. Argv form (no shell) also removes the
// injection surface entirely. `savings` tolerates a trailing 'd' on the value.
inline std::vector<std::string> savingsArgv(const std::string& exe,
                                            const std::string& windowDays) {
    return {exe, "savings", "--json", "--window", windowDays};
}

// Pure parse of `icmg savings --json` output into BadgeData.
// On any parse failure, returns zero-init BadgeData -> empty-data guard fires.
inline BadgeData parseSavingsJson(const std::string& s) {
    BadgeData d;
    try {
        auto j = nlohmann::json::parse(s);
        BadgeData tmp;  // stage into temp; commit only on full success (atomic)
        if (j.contains("total")) {
            const auto& t = j["total"];
            if (t.contains("calls") && t["calls"].is_number()) tmp.total_calls  = t["calls"].get<long long>();
            if (t.contains("saved") && t["saved"].is_number()) tmp.saved_tokens = t["saved"].get<long long>();
            if (t.contains("pct")   && t["pct"].is_number())   tmp.pct          = t["pct"].get<double>();
        }
        if (j.contains("cost") && j["cost"].contains("saved") && j["cost"]["saved"].is_number())
            tmp.cost_saved = j["cost"]["saved"].get<double>();
        d = tmp;  // all extraction succeeded -> commit
    } catch (...) { /* leave zero-init -> empty-data guard fires */ }
    return d;
}

inline std::string renderBadge(const std::string& metric, const BadgeData& d) {
    // Empty-data guard: no calls recorded -> "no data"/lightgrey (before metric dispatch).
    if (d.total_calls <= 0) {
        const char* label = (metric == "tokens") ? "tokens saved"
                          : (metric == "cost")   ? "cost saved"
                                                 : "token saved";
        return endpointJson(label, "no data", "lightgrey");
    }
    if (metric == "tokens")
        return endpointJson("tokens saved", humanizeTokens(d.saved_tokens), tokensColor(d.saved_tokens));
    if (metric == "cost")
        return endpointJson("cost saved", "$" + oneDecimal(d.cost_saved), costColor(d.cost_saved));
    // default + "savings"
    return endpointJson("token saved", oneDecimal(d.pct) + "%", savingsColor(d.pct));
}

}  // namespace icmg::cli
