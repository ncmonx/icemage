// Phase 20: token-budget tracker. Phase 21 Task 9: HTML analytics.
//   icmg budget                  — usage report (last 24h default)
//   icmg budget --window 7d
//   icmg budget record <tool> --raw N --filtered M [--cmd "..."]
//   icmg budget by-tool / by-cmd
//   icmg budget --html [--out FILE]   — HTML dashboard (Phase 21 T9)

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <map>
#include <fstream>

namespace icmg::cli {

static int64_t bdgNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ~4 chars/token (cl100k-ish heuristic; not exact)
static int64_t bytesToTokens(int64_t bytes) { return (bytes + 3) / 4; }

// Parse "24h" / "7d" / "30m" → seconds
static int64_t parseWindow(const std::string& s) {
    if (s.empty()) return 24 * 3600;
    int64_t n = 0;
    size_t i = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { n = n * 10 + (s[i] - '0'); ++i; }
    if (i == s.size()) return n;
    char unit = s[i];
    if (unit == 'm') return n * 60;
    if (unit == 'h') return n * 3600;
    if (unit == 'd') return n * 86400;
    return n;
}

static std::string fmtKB(int64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + "K";
    return std::to_string(bytes / (1024 * 1024)) + "M";
}

class BudgetCommand : public BaseCommand {
public:
    std::string name()        const override { return "budget"; }
    std::string description() const override { return "Token-budget tracker (per-tool savings + hot spots)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg budget [options]\n\n"
            "Modes:\n"
            "  (default)                      Window report (last 24h)\n"
            "  --window <Nh|Nd|Nm>            Time window (default 24h)\n"
            "  by-tool                        Group by tool_name\n"
            "  by-cmd [--limit N]             Top N commands by tokens\n"
            "  record <tool> --raw N [--filtered M] [--cmd \"...\"]\n"
            "                                 Manually log an invocation\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (!args.empty() && args[0] == "--help") { usage(); return 0; }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        std::string sub = args.empty() ? "" : args[0];
        std::vector<std::string> rest = args.empty()
            ? std::vector<std::string>{}
            : std::vector<std::string>(args.begin() + 1, args.end());

        if (sub == "record") {
            if (rest.empty()) { std::cerr << "budget record: requires <tool>\n"; return 1; }
            std::string tool = rest[0];
            int64_t raw = 0, filt = 0;
            try { raw  = std::stoll(flagValue(rest, "--raw", "0")); } catch (...) {}
            try { filt = std::stoll(flagValue(rest, "--filtered", "0")); } catch (...) {}
            std::string cmd = flagValue(rest, "--cmd");
            int64_t in_tok  = bytesToTokens(raw);
            int64_t out_tok = filt > 0 ? bytesToTokens(filt) : in_tok;
            int64_t saved   = filt > 0 ? bytesToTokens(raw - filt) : 0;
            db.run(
                "INSERT INTO tool_invocations(tool_name,command,raw_bytes,filtered_bytes,"
                " est_tokens_in,est_tokens_out,saved_tokens) VALUES(?,?,?,?,?,?,?)",
                {tool, cmd, std::to_string(raw), std::to_string(filt),
                 std::to_string(in_tok), std::to_string(out_tok), std::to_string(saved)});
            std::cout << "Recorded " << tool
                      << "  in=" << in_tok << " out=" << out_tok
                      << " saved=" << saved << " tokens\n";
            return 0;
        }

        if (sub == "by-tool") {
            int64_t window = parseWindow(flagValue(rest, "--window", "24h"));
            int64_t since  = bdgNow() - window;
            std::cout << "=== Budget by tool (last " << window/60 << "m) ===\n";
            std::cout << "  " << std::left << std::setw(20) << "TOOL"
                      << std::setw(8)  << "CALLS"
                      << std::setw(12) << "RAW"
                      << std::setw(12) << "FILTERED"
                      << std::setw(12) << "SAVED-tok\n";
            db.query(
                "SELECT tool_name, COUNT(*), SUM(raw_bytes), SUM(filtered_bytes), SUM(saved_tokens)"
                " FROM tool_invocations WHERE timestamp >= ?"
                " GROUP BY tool_name ORDER BY SUM(saved_tokens) DESC",
                {std::to_string(since)},
                [&](const core::Row& r) {
                    if (r.size() < 5) return;
                    std::cout << "  " << std::left << std::setw(20) << r[0]
                              << std::setw(8)  << r[1]
                              << std::setw(12) << fmtKB(std::stoll(r[2].empty() ? "0" : r[2]))
                              << std::setw(12) << fmtKB(std::stoll(r[3].empty() ? "0" : r[3]))
                              << std::setw(12) << r[4] << "\n";
                });
            return 0;
        }

        if (sub == "by-cmd") {
            int64_t window = parseWindow(flagValue(rest, "--window", "24h"));
            int64_t since  = bdgNow() - window;
            int limit = 10;
            try { limit = std::stoi(flagValue(rest, "--limit", "10")); } catch (...) {}
            std::cout << "=== Top commands (last " << window/3600 << "h, top " << limit << ") ===\n";
            db.query(
                "SELECT command, COUNT(*), SUM(saved_tokens) FROM tool_invocations"
                " WHERE timestamp >= ? AND command IS NOT NULL AND command != ''"
                " GROUP BY command ORDER BY SUM(saved_tokens) DESC LIMIT ?",
                {std::to_string(since), std::to_string(limit)},
                [&](const core::Row& r) {
                    if (r.size() < 3) return;
                    std::cout << "  [" << r[1] << "x] " << r[2]
                              << " tok saved  " << r[0].substr(0, 60) << "\n";
                });
            return 0;
        }

        // HTML dashboard (Phase 21 Task 9)
        if (hasFlag(args, "--html")) {
            std::string out_file = flagValue(args, "--out", "icmg-budget.html");
            int64_t window = parseWindow(flagValue(args, "--window", "7d"));
            int64_t since  = bdgNow() - window;

            // Per-tool aggregation
            struct ToolStat { int64_t calls=0, raw=0, filtered=0, saved=0; };
            std::map<std::string, ToolStat> by_tool;
            // Per-day timeline
            std::map<int64_t, int64_t> day_in, day_out, day_saved;
            db.query(
                "SELECT timestamp, tool_name, raw_bytes, filtered_bytes,"
                " est_tokens_in, est_tokens_out, saved_tokens"
                " FROM tool_invocations WHERE timestamp >= ?",
                {std::to_string(since)},
                [&](const core::Row& r) {
                    if (r.size() < 7) return;
                    try {
                        int64_t ts = std::stoll(r[0]);
                        int64_t day = ts - (ts % 86400);
                        std::string tool = r[1];
                        int64_t raw  = r[2].empty() ? 0 : std::stoll(r[2]);
                        int64_t filt = r[3].empty() ? 0 : std::stoll(r[3]);
                        int64_t in_t = r[4].empty() ? 0 : std::stoll(r[4]);
                        int64_t out_t= r[5].empty() ? 0 : std::stoll(r[5]);
                        int64_t sv   = r[6].empty() ? 0 : std::stoll(r[6]);
                        auto& ts2 = by_tool[tool];
                        ++ts2.calls; ts2.raw += raw; ts2.filtered += filt; ts2.saved += sv;
                        day_in[day]    += in_t;
                        day_out[day]   += out_t;
                        day_saved[day] += sv;
                    } catch (...) {}
                });

            std::ofstream f(out_file);
            if (!f) { std::cerr << "icmg budget: cannot open " << out_file << "\n"; return 1; }

            f << "<!DOCTYPE html><html><head><meta charset='utf-8'>"
              << "<title>icmg budget</title>"
              << "<style>"
              << "body{font-family:system-ui;background:#1a1a2e;color:#e0e0e0;padding:20px;}"
              << "h1{color:#4fc3f7;}h2{color:#9bb8e8;border-bottom:1px solid #2a2a4a;padding-bottom:6px;}"
              << "table{border-collapse:collapse;margin:16px 0;}"
              << "th,td{padding:6px 14px;border-bottom:1px solid #2a2a4a;text-align:left;}"
              << "th{color:#4fc3f7;}"
              << ".bar{height:18px;background:linear-gradient(90deg,#42a5f5,#a855f7);display:inline-block;}"
              << ".muted{color:#9e9e9e;font-size:13px;}"
              << "</style></head><body>"
              << "<h1>icmg token budget</h1>"
              << "<p class='muted'>Window: last " << window/3600 << "h</p>";

            // Top tools table with bars
            f << "<h2>By tool</h2><table>"
              << "<tr><th>Tool</th><th>Calls</th><th>Raw bytes</th>"
              << "<th>Filtered bytes</th><th>Tokens saved</th><th>Reduction</th></tr>";
            int64_t max_saved = 1;
            for (auto& [t, s] : by_tool) if (s.saved > max_saved) max_saved = s.saved;
            for (auto& [t, s] : by_tool) {
                double pct = s.raw > 0 ? 100.0 * (double)(s.raw - s.filtered) / (double)s.raw : 0.0;
                int barw = (int)(200.0 * (double)s.saved / (double)max_saved);
                f << "<tr><td>" << t << "</td><td>" << s.calls << "</td>"
                  << "<td>" << s.raw << "</td><td>" << s.filtered << "</td>"
                  << "<td>" << s.saved << " <span class='bar' style='width:" << barw << "px'></span></td>"
                  << "<td>" << std::fixed << std::setprecision(1) << pct << "%</td></tr>";
            }
            f << "</table>";

            // Daily timeline
            f << "<h2>Daily timeline</h2><table>"
              << "<tr><th>Day (UTC)</th><th>Tokens in</th><th>Tokens out</th><th>Saved</th></tr>";
            int64_t max_day = 1;
            for (auto& [d, v] : day_in) if (v > max_day) max_day = v;
            for (auto& [d, in_t] : day_in) {
                int64_t out_t = day_out[d];
                int64_t sv    = day_saved[d];
                int barw = (int)(200.0 * (double)in_t / (double)max_day);
                char dbuf[32];
                time_t tt = (time_t)d;
                struct tm* gm = gmtime(&tt);
                if (gm) strftime(dbuf, sizeof(dbuf), "%Y-%m-%d", gm); else snprintf(dbuf, sizeof(dbuf), "%lld", (long long)d);
                f << "<tr><td>" << dbuf << "</td>"
                  << "<td>" << in_t << " <span class='bar' style='width:" << barw << "px'></span></td>"
                  << "<td>" << out_t << "</td><td>" << sv << "</td></tr>";
            }
            f << "</table>";
            f << "<p class='muted'>Generated by icmg budget --html</p>";
            f << "</body></html>";
            std::cout << "Wrote " << out_file << "\n";
            return 0;
        }

        // default: window summary
        int64_t window = parseWindow(flagValue(args, "--window", "24h"));
        int64_t since  = bdgNow() - window;
        int total_calls = 0;
        int64_t total_in = 0, total_out = 0, total_saved = 0;
        db.query(
            "SELECT COUNT(*), SUM(est_tokens_in), SUM(est_tokens_out), SUM(saved_tokens)"
            " FROM tool_invocations WHERE timestamp >= ?",
            {std::to_string(since)},
            [&](const core::Row& r) {
                if (r.size() < 4) return;
                try {
                    total_calls = std::stoi(r[0]);
                    total_in    = r[1].empty() ? 0 : std::stoll(r[1]);
                    total_out   = r[2].empty() ? 0 : std::stoll(r[2]);
                    total_saved = r[3].empty() ? 0 : std::stoll(r[3]);
                } catch (...) {}
            });
        std::cout << "=== Token budget (last " << window/3600 << "h) ===\n"
                  << "Calls:           " << total_calls << "\n"
                  << "Tokens in:       " << total_in << "\n"
                  << "Tokens out:      " << total_out << "\n"
                  << "Tokens saved:    " << total_saved << "\n";
        if (total_in > 0) {
            double pct = 100.0 * (double)total_saved / (double)total_in;
            std::cout << "Reduction:       " << std::fixed << std::setprecision(1) << pct << "%\n";
        }
        std::cout << "\nUse `icmg budget by-tool` for breakdown.\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("budget", BudgetCommand);

} // namespace icmg::cli
