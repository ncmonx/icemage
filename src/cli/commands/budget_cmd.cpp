// Phase 20: token-budget tracker.
//   icmg budget                  — usage report (last 24h default)
//   icmg budget --window 7d
//   icmg budget record <tool> --raw N --filtered M [--cmd "..."]
//   icmg budget by-tool / by-cmd

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <map>

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
