// `icmg savings` — unified token-savings history.
//
// Aggregates three telemetry tables:
//   - tool_invocations    (icmg run filtering)
//   - compression_telemetry (icmg compress)
//   - thinking_telemetry  (pack --no-think directives)
//
// Shows side-by-side "without icmg" baseline vs "with icmg" actual, with
// percent saved + dollar value. Console output (default) or self-contained
// HTML dashboard (--html).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>
#include <algorithm>

namespace icmg::cli {

namespace {

struct Bucket {
    int64_t calls = 0;
    int64_t raw_tokens = 0;       // hypothetical "without icmg"
    int64_t actual_tokens = 0;    // "with icmg"
    int64_t saved = 0;
};

double pct(int64_t saved, int64_t raw) {
    if (raw <= 0) return 0.0;
    return 100.0 * (double)saved / (double)raw;
}

double dollars(int64_t tokens, double rate_per_mtok = 3.0) {
    return (double)tokens * rate_per_mtok / 1'000'000.0;
}

} // namespace

class SavingsCommand : public BaseCommand {
public:
    std::string name()        const override { return "savings"; }
    std::string description() const override {
        return "Unified token-savings history (filter + compress + thinking)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg savings [options]\n\n"
            "Options:\n"
            "  --window <Nd>      Time window (default 30d)\n"
            "  --html [-o FILE]   Generate self-contained HTML dashboard\n"
            "  --json             Machine output\n"
            "  --rate-input N     Input $/MTok (default 3.0 = Sonnet)\n"
            "  --rate-output N    Output $/MTok (default 15.0)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }

        int window_days = 30;
        try {
            std::string w = flagValue(args, "--window");
            if (!w.empty()) {
                if (!w.empty() && w.back() == 'd') w.pop_back();
                window_days = std::stoi(w);
            }
        } catch (...) {}

        bool html = hasFlag(args, "--html");
        bool json_out = hasFlag(args, "--json");
        std::string out_path = flagValue(args, "-o");
        double rate_in = 3.0, rate_out = 15.0;
        try { auto v = flagValue(args, "--rate-input");  if (!v.empty()) rate_in  = std::stod(v); } catch (...) {}
        try { auto v = flagValue(args, "--rate-output"); if (!v.empty()) rate_out = std::stod(v); } catch (...) {}

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        int64_t cutoff = (int64_t)std::time(nullptr) - (int64_t)window_days * 86400;

        Bucket filter, compress, thinking;

        try {
            db.query("SELECT COUNT(*), COALESCE(SUM(raw_bytes),0), COALESCE(SUM(filtered_bytes),0), COALESCE(SUM(saved_tokens),0) "
                     "FROM tool_invocations WHERE timestamp > ?",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){
                         if (r.size() < 4) return;
                         filter.calls = std::stoll(r[0]);
                         int64_t raw_bytes      = std::stoll(r[1]);
                         int64_t filtered_bytes = std::stoll(r[2]);
                         filter.saved = std::stoll(r[3]);
                         // bytes/4 ≈ tokens
                         filter.raw_tokens    = raw_bytes / 4;
                         filter.actual_tokens = filtered_bytes / 4;
                         if (filter.actual_tokens == 0 && raw_bytes > 0)
                             filter.actual_tokens = filter.raw_tokens - filter.saved;
                     });
        } catch (...) {}

        try {
            db.query("SELECT COUNT(*), COALESCE(SUM(tok_in),0), COALESCE(SUM(tok_out),0) "
                     "FROM compression_telemetry WHERE created_at > ? AND mode != 'skipped'",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){
                         if (r.size() < 3) return;
                         compress.calls = std::stoll(r[0]);
                         compress.raw_tokens = std::stoll(r[1]);
                         compress.actual_tokens = std::stoll(r[2]);
                         compress.saved = compress.raw_tokens - compress.actual_tokens;
                     });
        } catch (...) {}

        try {
            db.query("SELECT COUNT(*), SUM(no_think) "
                     "FROM thinking_telemetry WHERE created_at > ?",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){
                         if (r.size() < 2) return;
                         thinking.calls = std::stoll(r[0]);
                         int64_t nt_calls = r[1].empty() ? 0 : std::stoll(r[1]);
                         // Estimate: 1500 thinking tokens saved per no-think call.
                         thinking.saved = nt_calls * 1500;
                         thinking.raw_tokens    = thinking.calls * 1500;
                         thinking.actual_tokens = thinking.raw_tokens - thinking.saved;
                     });
        } catch (...) {}

        Bucket total;
        total.calls         = filter.calls + compress.calls + thinking.calls;
        total.raw_tokens    = filter.raw_tokens + compress.raw_tokens + thinking.raw_tokens;
        total.actual_tokens = filter.actual_tokens + compress.actual_tokens + thinking.actual_tokens;
        total.saved         = filter.saved + compress.saved + thinking.saved;

        // Cost estimate: filter+compress = input price; thinking = output price.
        // Phase 67 T25: split cost by input vs output rates.
        double cost_in_without  = dollars(filter.raw_tokens + compress.raw_tokens, rate_in);
        double cost_in_with     = dollars(filter.actual_tokens + compress.actual_tokens, rate_in);
        double cost_out_without = dollars(thinking.raw_tokens, rate_out);
        double cost_out_with    = dollars(thinking.actual_tokens, rate_out);
        double cost_without = cost_in_without + cost_out_without;
        double cost_with    = cost_in_with    + cost_out_with;
        double cost_saved   = cost_without - cost_with;

        // Phase 58: aggregate strict-mode denials within window.
        auto denials = readStrictDenials(cutoff);

        if (json_out) {
            std::cout << "{\"window_days\":" << window_days
                      << ",\"total\":{\"calls\":" << total.calls
                      << ",\"raw\":" << total.raw_tokens
                      << ",\"actual\":" << total.actual_tokens
                      << ",\"saved\":" << total.saved
                      << ",\"pct\":" << std::fixed << std::setprecision(1) << pct(total.saved, total.raw_tokens) << "}"
                      << ",\"cost\":{\"without\":" << cost_without
                      << ",\"with\":" << cost_with
                      << ",\"saved\":" << cost_saved << "}"
                      << ",\"breakdown\":{"
                      << "\"filter\":{\"calls\":" << filter.calls << ",\"saved\":" << filter.saved << "},"
                      << "\"compress\":{\"calls\":" << compress.calls << ",\"saved\":" << compress.saved << "},"
                      << "\"thinking\":{\"calls\":" << thinking.calls << ",\"saved\":" << thinking.saved << "}}"
                      << ",\"strict_denials\":{\"total\":" << denials.total;
            for (auto& kv : denials.by_hook)
                std::cout << ",\"" << kv.first << "\":" << kv.second;
            std::cout << "}}\n";
            return 0;
        }

        if (html) return emitHtml(filter, compress, thinking, total,
                                   cost_without, cost_with, cost_saved,
                                   window_days, out_path);

        // Console output
        std::cout << "\nicmg savings — last " << window_days << " days\n"
                  << std::string(64, '=') << "\n\n";
        renderRow("Command filter (icmg run)",      filter);
        renderRow("Compression  (icmg compress)",   compress);
        renderRow("Thinking     (--no-think)",      thinking);
        std::cout << std::string(64, '-') << "\n";
        renderRow("TOTAL",                            total);
        std::cout << "\n"
                  << "Cost without icmg: $" << std::fixed << std::setprecision(2) << cost_without
                  << "  (input $" << cost_in_without << " / output $" << cost_out_without << ")\n"
                  << "Cost with    icmg: $" << cost_with
                  << "  (input $" << cost_in_with    << " / output $" << cost_out_with    << ")\n"
                  << "You saved:         $" << cost_saved
                  << "  (" << std::fixed << std::setprecision(1)
                  << pct(total.saved, total.raw_tokens) << "%)\n";

        if (denials.total > 0) {
            std::cout << "\nStrict-mode denials in window: " << denials.total << "\n";
            for (auto& kv : denials.by_hook) {
                std::cout << "  " << std::left << std::setw(20) << kv.first
                          << std::right << std::setw(6) << kv.second << " blocked\n";
            }
            std::cout << "  → each block redirected agent to icmg context/fetch\n";
        }
        std::cout << "\n";
        return 0;
    }

private:
    // Phase 58: read ~/.icmg/strict-denials.jsonl, count by hook within window.
    struct DenialBucketImpl { int64_t total = 0; std::map<std::string,int64_t> by_hook; };
    DenialBucketImpl readStrictDenials(int64_t cutoff_ts) {
        DenialBucketImpl out;
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (!home) return out;
        std::filesystem::path log = std::filesystem::path(home) / ".icmg" / "strict-denials.jsonl";
        if (!std::filesystem::exists(log)) return out;
        std::ifstream f(log);
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            // Cheap parse: find "ts": and "hook":. Avoids json dep on hot path.
            auto ts_pos = line.find("\"ts\":");
            if (ts_pos == std::string::npos) continue;
            int64_t ts = 0;
            try { ts = std::stoll(line.substr(ts_pos + 5)); } catch (...) { continue; }
            if (ts < cutoff_ts) continue;
            auto h_pos = line.find("\"hook\":\"");
            if (h_pos == std::string::npos) continue;
            h_pos += 8;
            auto h_end = line.find('"', h_pos);
            if (h_end == std::string::npos) continue;
            std::string hook = line.substr(h_pos, h_end - h_pos);
            ++out.total;
            ++out.by_hook[hook];
        }
        return out;
    }

    static void renderRow(const std::string& label, const Bucket& b) {
        std::cout << "  " << std::left << std::setw(32) << label
                  << std::right << std::setw(8) << b.calls << " calls "
                  << std::setw(11) << b.raw_tokens    << " → "
                  << std::setw(11) << b.actual_tokens
                  << "  (" << std::fixed << std::setprecision(0)
                  << pct(b.saved, b.raw_tokens) << "% saved)\n";
    }

    int emitHtml(const Bucket& f, const Bucket& c, const Bucket& t, const Bucket& tot,
                  double cost_without, double cost_with, double cost_saved,
                  int window_days, const std::string& out_path) {
        std::ostringstream os;
        os << R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Icemage savings</title>
<link rel="icon" type="image/svg+xml" href="data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'><defs><linearGradient id='f' x1='0' y1='0' x2='1' y2='1'><stop offset='0%25' stop-color='%237DD3FC'/><stop offset='100%25' stop-color='%234338CA'/></linearGradient></defs><circle cx='16' cy='16' r='14' fill='none' stroke='url(%23f)' stroke-width='1.5'/><g stroke='url(%23f)' stroke-width='1.4' fill='none' stroke-linecap='round'><path d='M16 16 L16 7 M16 16 L9 12 M16 16 L23 12 M16 16 L9 20 M16 16 L23 20 M16 16 L16 25'/></g><polygon points='16,13 19,16 16,19 13,16' fill='url(%23f)'/></svg>"/>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font:14px/1.5 -apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;background:#0d1117;color:#c9d1d9;padding:32px;max-width:1100px;margin:0 auto}
h1{font-size:28px;margin-bottom:8px;color:#58a6ff}
.sub{color:#8b949e;margin-bottom:32px}
.hero{background:linear-gradient(135deg,#1f6feb,#388bfd);border-radius:12px;padding:32px;margin-bottom:32px;display:grid;grid-template-columns:1fr 1fr 1fr;gap:24px}
.hero .stat{text-align:center}
.hero .num{font-size:36px;font-weight:700;color:#fff}
.hero .lbl{font-size:13px;color:rgba(255,255,255,.85);margin-top:4px;text-transform:uppercase;letter-spacing:.05em}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px;margin-bottom:32px}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:20px}
.card h2{font-size:14px;color:#8b949e;text-transform:uppercase;letter-spacing:.05em;margin-bottom:12px;font-weight:500}
.card .pct{font-size:32px;font-weight:700;color:#3fb950;margin-bottom:4px}
.card .calls{color:#8b949e;font-size:12px}
.bar{height:24px;background:#21262d;border-radius:4px;overflow:hidden;margin:12px 0;position:relative}
.bar .fill{height:100%;background:linear-gradient(90deg,#3fb950,#56d364);transition:width .8s ease}
.bar .lbl-in,.bar .lbl-out{position:absolute;top:50%;transform:translateY(-50%);font-size:11px;font-weight:500;padding:0 8px}
.bar .lbl-in{left:0;color:#fff}.bar .lbl-out{right:0;color:#c9d1d9}
table{width:100%;border-collapse:collapse;background:#161b22;border:1px solid #30363d;border-radius:8px;overflow:hidden}
th{background:#21262d;padding:12px 16px;text-align:left;color:#8b949e;font-size:12px;text-transform:uppercase;letter-spacing:.05em;font-weight:500}
td{padding:14px 16px;border-top:1px solid #30363d}
td.num{text-align:right;font-variant-numeric:tabular-nums}
.footer{color:#6e7681;font-size:12px;margin-top:32px;text-align:center}
</style></head><body>
<h1>icmg — token savings dashboard</h1>
<div class="sub">Last )HTML"
           << window_days << R"HTML( days · without-icmg vs with-icmg comparison</div>
<div class="hero">
<div class="stat"><div class="num">$)HTML"
           << std::fixed << std::setprecision(2) << cost_saved
           << R"HTML(</div><div class="lbl">Cost saved</div></div>
<div class="stat"><div class="num">)HTML"
           << std::fixed << std::setprecision(0) << pct(tot.saved, tot.raw_tokens)
           << R"HTML(%</div><div class="lbl">Tokens saved</div></div>
<div class="stat"><div class="num">)HTML"
           << tot.calls
           << R"HTML(</div><div class="lbl">Total operations</div></div>
</div>
<div class="cards">)HTML";

        emitCard(os, "Command filtering",     "icmg run",       f);
        emitCard(os, "Prompt compression",    "icmg compress",  c);
        emitCard(os, "Thinking-budget cuts",  "pack --no-think", t);

        os << R"HTML(</div>
<table>
<thead><tr><th>Layer</th><th class="num">Calls</th><th class="num">Without icmg</th><th class="num">With icmg</th><th class="num">Saved</th><th class="num">% off</th></tr></thead>
<tbody>)HTML";
        emitRow(os, "Command filter",  f);
        emitRow(os, "Compression",     c);
        emitRow(os, "Thinking budget", t);
        os << "<tr style='font-weight:700;background:#21262d'>";
        emitRow(os, "Total",           tot, false);
        os << "</tr>";
        os << R"HTML(</tbody>
</table>

<h2 style="font-size:18px;color:#58a6ff;margin:32px 0 12px">Daily savings (last )HTML"
           << window_days << R"HTML( days)</h2>
)HTML";
        emitDailyChart(os, window_days);

        os << R"HTML(<div class="footer">Generated by `icmg savings --html` · costs estimated at $)HTML"
           << "3.00/MTok input + $15.00/MTok output (Sonnet 4.5 rate). Lower bound.</div>"
           << "</body></html>";

        std::string fp = out_path.empty() ? "savings.html" : out_path;
        std::ofstream of(fp);
        if (!of) {
            std::cerr << "icmg savings: write " << fp << " failed\n";
            return 1;
        }
        of << os.str();
        std::cout << "Dashboard written → " << fp << "\n";
        return 0;
    }

    // Phase 51: per-day saved-tokens bar chart, inline SVG, no JS dep.
    void emitDailyChart(std::ostream& os, int window_days) {
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        std::vector<std::pair<std::string, int64_t>> daily;  // (date, saved_tokens)
        // Aggregate per day across the 3 telemetry tables.
        // Each table has: created_at (epoch). Saved-token estimate per row varies.
        // For uniform aggregation, use:
        //   - filter telemetry: rows have raw_tokens - actual_tokens (cmd_runs table)
        //   - compression_telemetry: tok_in - tok_out
        //   - thinking_telemetry: 1500 per no_think=1 row (heuristic)
        std::map<std::string, int64_t> by_day;
        int64_t now_s = (int64_t)std::time(nullptr);
        int64_t cutoff = now_s - (int64_t)window_days * 86400;

        try {
            // tool_invocations stores saved_tokens column directly.
            db.query("SELECT date(timestamp,'unixepoch'), COALESCE(SUM(saved_tokens),0) "
                     "FROM tool_invocations WHERE timestamp > ? "
                     "GROUP BY date(timestamp,'unixepoch')",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){
                         if (r.size() >= 2 && !r[0].empty()) {
                             try { by_day[r[0]] += std::stoll(r[1]); } catch (...) {}
                         }
                     });
        } catch (...) {}
        try {
            db.query("SELECT date(created_at,'unixepoch'), "
                     "       SUM(COALESCE(tok_in,0)-COALESCE(tok_out,0)) "
                     "FROM compression_telemetry WHERE created_at > ? "
                     "GROUP BY date(created_at,'unixepoch')",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){
                         if (r.size() >= 2 && !r[0].empty()) {
                             try { by_day[r[0]] += std::stoll(r[1]); } catch (...) {}
                         }
                     });
        } catch (...) {}
        try {
            db.query("SELECT date(created_at,'unixepoch'), "
                     "       SUM(no_think) * 1500 "
                     "FROM thinking_telemetry WHERE created_at > ? "
                     "GROUP BY date(created_at,'unixepoch')",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){
                         if (r.size() >= 2 && !r[0].empty()) {
                             try { by_day[r[0]] += std::stoll(r[1]); } catch (...) {}
                         }
                     });
        } catch (...) {}

        if (by_day.empty()) {
            os << "<div class='card' style='text-align:center;color:#8b949e;padding:40px'>"
               << "No daily activity recorded yet. Run `icmg run`, `icmg compress`, "
               << "`icmg pack --auto-think` to populate.</div>";
            return;
        }

        // Find max for scaling.
        int64_t max_v = 1;
        for (auto& [d, v] : by_day) if (v > max_v) max_v = v;

        // Render SVG with bars left-to-right, oldest → newest.
        const int W = 1040, H = 200, BAR_GAP = 2, MARGIN_TOP = 20, MARGIN_BOT = 30;
        int n = (int)by_day.size();
        if (n < 1) return;
        double bar_w = (double)(W - 40) / n - BAR_GAP;
        if (bar_w < 4) bar_w = 4;

        os << "<div class='card' style='padding:20px'>"
           << "<svg width='100%' viewBox='0 0 " << W << " " << H
           << "' style='display:block'>";
        // Y-axis grid lines (4 levels).
        for (int i = 0; i <= 4; ++i) {
            int y = MARGIN_TOP + (int)((H - MARGIN_TOP - MARGIN_BOT) * i / 4.0);
            os << "<line x1='30' x2='" << (W - 10) << "' y1='" << y << "' y2='" << y
               << "' stroke='#30363d' stroke-width='1'/>";
            int64_t lvl = max_v * (4 - i) / 4;
            os << "<text x='25' y='" << (y + 4)
               << "' fill='#6e7681' font-size='10' text-anchor='end'>"
               << humanTok(lvl) << "</text>";
        }
        // Bars.
        int idx = 0;
        for (auto& [d, v] : by_day) {
            double x = 35 + idx * (bar_w + BAR_GAP);
            double h = (H - MARGIN_TOP - MARGIN_BOT) * (double)v / (double)max_v;
            double y = H - MARGIN_BOT - h;
            os << "<rect x='" << x << "' y='" << y << "' width='" << bar_w
               << "' height='" << h
               << "' fill='url(#barg)' rx='2'>"
               << "<title>" << d << ": " << humanTok(v) << " tok saved</title>"
               << "</rect>";
            // Date label every Nth column (avoid clutter).
            int label_step = std::max(1, n / 10);
            if (idx % label_step == 0) {
                os << "<text x='" << (x + bar_w / 2) << "' y='" << (H - 8)
                   << "' fill='#6e7681' font-size='9' text-anchor='middle'>"
                   << d.substr(5)  // MM-DD
                   << "</text>";
            }
            ++idx;
        }
        // Gradient defs.
        os << "<defs><linearGradient id='barg' x1='0' y1='0' x2='0' y2='1'>"
           << "<stop offset='0%' stop-color='#56d364'/>"
           << "<stop offset='100%' stop-color='#3fb950'/>"
           << "</linearGradient></defs>";
        os << "</svg>"
           << "<div style='margin-top:8px;color:#8b949e;font-size:12px;text-align:center'>"
           << "Daily tokens saved · " << by_day.size() << " active days</div></div>";
    }

    static void emitCard(std::ostream& os, const std::string& title,
                          const std::string& cmd, const Bucket& b) {
        double p = pct(b.saved, b.raw_tokens);
        os << "<div class='card'><h2>" << title << "</h2>"
           << "<div class='pct'>" << std::fixed << std::setprecision(0) << p << "% off</div>"
           << "<div class='calls'>" << b.calls << " ops · <code>" << cmd << "</code></div>"
           << "<div class='bar'><div class='fill' style='width:" << p << "%'></div>"
           << "<span class='lbl-in'>" << humanTok(b.actual_tokens) << "</span>"
           << "<span class='lbl-out'>" << humanTok(b.raw_tokens) << "</span></div></div>";
    }

    static void emitRow(std::ostream& os, const std::string& label,
                         const Bucket& b, bool wrap = true) {
        if (wrap) os << "<tr>";
        os << "<td>" << label << "</td>"
           << "<td class='num'>" << b.calls << "</td>"
           << "<td class='num'>" << humanTok(b.raw_tokens) << "</td>"
           << "<td class='num'>" << humanTok(b.actual_tokens) << "</td>"
           << "<td class='num'>" << humanTok(b.saved) << "</td>"
           << "<td class='num'>" << std::fixed << std::setprecision(0)
           << pct(b.saved, b.raw_tokens) << "%</td>";
        if (wrap) os << "</tr>";
    }

    static std::string humanTok(int64_t n) {
        std::ostringstream os;
        if (n >= 1'000'000)      os << std::fixed << std::setprecision(1) << (n / 1'000'000.0) << "M";
        else if (n >= 1'000)     os << std::fixed << std::setprecision(1) << (n / 1'000.0) << "K";
        else                     os << n;
        return os.str();
    }
};

ICMG_REGISTER_COMMAND("savings", SavingsCommand);

} // namespace icmg::cli
