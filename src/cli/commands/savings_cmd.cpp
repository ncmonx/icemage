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
#include "../../core/exec_utils.hpp"
#ifdef _WIN32
  #include <windows.h>
#endif

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
    // Phase 68: extra layer buckets stashed for emitHtml (avoids large sig change).
    Bucket pack_recpt_, strict_block_, fetch_cache_, image_cache_;
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
            "  --ascii            ASCII sparkline (v1.20.0)\n"
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
        if (hasFlag(args, "--per-cmd")) {
            auto& cfg2 = core::Config::instance();
            core::Db db2(cfg2.projectDbPath("."));
            int64_t cutoff2 = (int64_t)std::time(nullptr) - (int64_t)window_days * 86400;
            std::cout << "=== Per-command savings (last " << window_days << "d) ===" << "\n";
            std::cout << "  TOOL          | CALLS | SAVED_TOK | RAW_TOK" << "\n";
            db2.query("SELECT tool_name, COUNT(*), COALESCE(SUM(saved_tokens),0), COALESCE(SUM(raw_bytes),0)/4 FROM tool_invocations WHERE timestamp > ? GROUP BY tool_name ORDER BY SUM(saved_tokens) DESC LIMIT 30",
                {std::to_string(cutoff2)},
                [&](const core::Row& r){
                    if (r.size() < 4) return;
                    std::cout << "  " << r[0] << " | " << r[1] << " | saved=" << r[2] << " | raw=" << r[3] << "\n";
                });
            return 0;
        }
                std::string out_path = flagValue(args, "-o");
        double rate_in = 3.0, rate_out = 15.0;
        try { auto v = flagValue(args, "--rate-input");  if (!v.empty()) rate_in  = std::stod(v); } catch (...) {}
        try { auto v = flagValue(args, "--rate-output"); if (!v.empty()) rate_out = std::stod(v); } catch (...) {}

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        int64_t cutoff = (int64_t)std::time(nullptr) - (int64_t)window_days * 86400;

        Bucket filter, compress, thinking, pack_recpt, strict_block, fetch_cache, image_cache, graph_bfs;

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

        // Phase 68: pack-receipt rows (Phase 67 T1) — covers per-pack memory/
        // graph/file hit token cost. Treats whole receipt as "with-icmg actual"
        // since receipts only fire when pack actually used.
        try {
            db.query(
                     // total_calls | actual for tracked rows | raw for tracked rows
                     "SELECT COUNT(*), "
                     "  COALESCE(SUM(CASE WHEN raw_tokens>0 THEN est_tokens ELSE 0 END),0), "
                     "  COALESCE(SUM(CASE WHEN raw_tokens>0 THEN raw_tokens ELSE 0 END),0) "
                     "FROM token_receipts WHERE ts > ?",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){
                         if (r.size() < 3) return;
                         pack_recpt.calls         = std::stoll(r[0]);
                         pack_recpt.actual_tokens = std::stoll(r[1]);
                         pack_recpt.raw_tokens    = std::stoll(r[2]);
                         pack_recpt.saved         = pack_recpt.raw_tokens - pack_recpt.actual_tokens;
                         if (pack_recpt.saved < 0) pack_recpt.saved = 0;
                     });
        } catch (...) {}

        // Phase 68: strict-mode denials count (Phase 58). Each block redirects
        // agent to icmg context/fetch — count the redirect as 1500 tokens
        // saved on average (raw Read of file user would otherwise have).
        {
            const char* h = std::getenv("USERPROFILE");
            if (!h) h = std::getenv("HOME");
            if (h) {
                std::filesystem::path log = std::filesystem::path(h) / ".icmg" / "strict-denials.jsonl";
                if (std::filesystem::exists(log)) {
                    std::ifstream f(log);
                    std::string line;
                    int64_t denial_count = 0;
                    while (std::getline(f, line)) {
                        auto pos = line.find("\"ts\":");
                        if (pos == std::string::npos) continue;
                        try {
                            int64_t ts = std::stoll(line.substr(pos + 5));
                            if (ts >= cutoff) ++denial_count;
                        } catch (...) {}
                    }
                    strict_block.calls = denial_count;
                    strict_block.raw_tokens = denial_count * 1500;
                    strict_block.saved = denial_count * 1500;
                    strict_block.actual_tokens = 0;
                }
            }
        }

        // Phase 68: fetch_cache hit savings — each cache hit = avoided
        // re-download of (typically reduced) HTML payload.
        try {
            db.query("SELECT COUNT(*), COALESCE(SUM(bytes_in - bytes_out),0) FROM fetch_cache "
                     "WHERE expires_at > ?",
                     {std::to_string(std::time(nullptr))},
                     [&](const core::Row& r){
                         if (r.size() < 2) return;
                         fetch_cache.calls = std::stoll(r[0]);
                         int64_t bytes_saved = std::stoll(r[1]);
                         fetch_cache.saved = bytes_saved / 4;
                         fetch_cache.raw_tokens = fetch_cache.saved;
                         fetch_cache.actual_tokens = 0;
                     });
        } catch (...) {}

        // Phase 68: image_cache hit savings (OCR sidecar reuse).
        try {
            db.query("SELECT COUNT(*), COALESCE(SUM(LENGTH(text_extracted)),0) FROM image_cache "
                     "WHERE created_at > ?",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){
                         if (r.size() < 2) return;
                         image_cache.calls = std::stoll(r[0]);
                         // OCR'd text = extracted; vision-API tokens saved ~10× text size.
                         int64_t text_bytes = std::stoll(r[1]);
                         image_cache.saved = (text_bytes * 10) / 4;
                         image_cache.raw_tokens = image_cache.saved;
                         image_cache.actual_tokens = 0;
                     });
        } catch (...) {}

        // v0.53.0: graph BFS savings — each call (graph-path/layers/neighbors/common)
        // replaces manually reading ~4 source files; estimate 2000 tokens saved/call.
        {
            const char* h = std::getenv("USERPROFILE");
            if (!h) h = std::getenv("HOME");
            if (h) {
                std::filesystem::path log = std::filesystem::path(h) / ".icmg" / "bfs-queries.jsonl";
                if (std::filesystem::exists(log)) {
                    std::ifstream f(log);
                    std::string line;
                    while (std::getline(f, line)) {
                        auto pos = line.find("\"ts\":");
                        if (pos == std::string::npos) continue;
                        try {
                            int64_t ts = std::stoll(line.substr(pos + 5));
                            if (ts >= cutoff) ++graph_bfs.calls;
                        } catch (...) {}
                    }
                    graph_bfs.raw_tokens    = graph_bfs.calls * 2000;
                    graph_bfs.saved         = graph_bfs.calls * 2000;
                    graph_bfs.actual_tokens = 0;
                }
            }
        }

        Bucket total;
        total.calls         = filter.calls + compress.calls + thinking.calls
                            + pack_recpt.calls + strict_block.calls
                            + fetch_cache.calls + image_cache.calls + graph_bfs.calls;
        total.raw_tokens    = filter.raw_tokens + compress.raw_tokens + thinking.raw_tokens
                            + pack_recpt.raw_tokens + strict_block.raw_tokens
                            + fetch_cache.raw_tokens + image_cache.raw_tokens
                            + graph_bfs.raw_tokens;
        total.actual_tokens = filter.actual_tokens + compress.actual_tokens + thinking.actual_tokens
                            + pack_recpt.actual_tokens + strict_block.actual_tokens
                            + fetch_cache.actual_tokens + image_cache.actual_tokens
                            + graph_bfs.actual_tokens;
        total.saved         = filter.saved + compress.saved + thinking.saved
                            + pack_recpt.saved + strict_block.saved
                            + fetch_cache.saved + image_cache.saved
                            + graph_bfs.saved;

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

        // Phase 68: stash new buckets for emitHtml via member fields.
        pack_recpt_   = pack_recpt;
        strict_block_ = strict_block;
        fetch_cache_  = fetch_cache;
        image_cache_  = image_cache;
        if (html) return emitHtml(filter, compress, thinking, total,
                                   cost_without, cost_with, cost_saved,
                                   window_days, out_path);

        // Console output
        std::cout << "\nicmg savings — last " << window_days << " days\n"
                  << std::string(64, '=') << "\n\n";
        renderRow("Command filter (icmg run)",      filter);
        renderRow("Compression  (icmg compress)",   compress);
        renderRow("Thinking     (--no-think)",      thinking);
        if (pack_recpt.calls > 0)   renderRow("Pack receipts  (memory+graph)", pack_recpt);
        if (strict_block.calls > 0) renderRow("Strict denials (read/web/bash)", strict_block);
        if (fetch_cache.calls > 0)  renderRow("Fetch cache    (icmg fetch)", fetch_cache);
        if (image_cache.calls > 0)  renderRow("Image OCR cache(icmg ingest)", image_cache);
        if (graph_bfs.calls > 0)    renderRow("Graph BFS       (path/layers)", graph_bfs);
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

        // v1.5.0: real session total (aggregated across all project sessions).
        RealSessionData rsd = fetchRealSessionData();
        if (rsd.total_tokens > 0) {
            int coverage_pct = rsd.total_tokens > 0
                                ? (int)(100 * total.raw_tokens / rsd.total_tokens) : 0;
            if (coverage_pct > 100) coverage_pct = 100;
            std::cout << "\nReal session tokens (sum of " << rsd.session_count
                      << " sessions): " << rsd.total_tokens
                      << "  (icmg-covered " << total.raw_tokens
                      << " = " << coverage_pct << "%, outside "
                      << (rsd.total_tokens - total.raw_tokens) << ")\n";

            std::map<std::string, int64_t, std::greater<std::string>> by_day;
            for (auto& r : rsd.sessions) {
                if (r.mtime <= 0) continue;
                std::time_t t = (std::time_t)r.mtime;
                std::tm tm_buf{};
#ifdef _WIN32
                localtime_s(&tm_buf, &t);
#else
                localtime_r(&t, &tm_buf);
#endif
                char dbuf[16]; std::strftime(dbuf, sizeof(dbuf), "%Y-%m-%d", &tm_buf);
                by_day[dbuf] += r.total;
            }
            if (!by_day.empty()) {
                // v1.20.0 (U1): ASCII sparkline. --ascii flag toggles compact bar view.
                bool ascii_mode = hasFlag(args, "--ascii");
                std::cout << "\nDaily real-token history (this project, newest first):\n";
                int shown = 0;
                if (ascii_mode) {
                    int64_t maxv = 0;
                    for (auto& [_, t] : by_day) if (t > maxv) maxv = t;
                    if (maxv == 0) maxv = 1;
                    for (auto& [d, tok] : by_day) {
                        if (shown++ >= 14) break;
                        int bars = (int)((tok * 30) / maxv);
                        std::string bar(bars > 0 ? bars : 0, '#');
                        std::cout << "  " << d << "  " << std::left << std::setw(30) << bar
                                  << std::right << std::setw(10) << tok << " tok\n";
                    }
                } else {
                    for (auto& [d, tok] : by_day) {
                        if (shown++ >= 14) break;
                        std::cout << "  " << d << "  " << std::setw(10) << tok << " tok\n";
                    }
                }
            }
        }

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

    // v1.5.0: real session data aggregated across ALL transcripts in the
    // current project's ~/.claude/projects/<cwd-encoded>/ directory.
    struct RealSessionRow {
        std::string file;
        std::string user;  // v1.9.0
        int64_t     mtime = 0;
        int64_t     total = 0;
        int64_t     text = 0;
        int64_t     tool_input = 0;
        int64_t     tool_output = 0;
        int64_t     thinking = 0;
    };
    struct RealSessionData {
        int64_t total_tokens = 0;
        int64_t text = 0;
        int64_t tool_input = 0;
        int64_t tool_output = 0;
        int64_t thinking = 0;
        int     session_count = 0;
        int     user_count = 0;  // v1.9.0
        std::vector<RealSessionRow> sessions;
    };

    static int64_t parseIntAfter(const std::string& s, const std::string& key,
                                   size_t start, size_t end) {
        size_t p = s.find(key, start);
        if (p == std::string::npos || p >= end) return 0;
        p += key.size();
        try { return std::stoll(s.substr(p)); } catch (...) { return 0; }
    }

    // Sum tokens for keys starting with prefix inside [scope_start, scope_end).
    // Each value object has shape {"calls":N,"tokens":M}.
    static int64_t sumSourceTokens(const std::string& s, size_t scope_start,
                                     size_t scope_end,
                                     const std::string& source_prefix,
                                     bool is_prefix_match) {
        int64_t out = 0;
        size_t pos = scope_start;
        while (pos < scope_end) {
            std::string needle = "\"" + source_prefix;
            size_t kp = s.find(needle, pos);
            if (kp == std::string::npos || kp >= scope_end) break;
            size_t after_prefix = kp + needle.size();
            if (!is_prefix_match) {
                if (after_prefix >= scope_end || s[after_prefix] != '"') {
                    pos = kp + needle.size();
                    continue;
                }
            }
            size_t obj_start = s.find('{', after_prefix);
            if (obj_start == std::string::npos || obj_start >= scope_end) break;
            size_t obj_end = s.find('}', obj_start);
            if (obj_end == std::string::npos || obj_end > scope_end) break;
            out += parseIntAfter(s, "\"tokens\":", obj_start, obj_end);
            pos = obj_end + 1;
        }
        return out;
    }

    static RealSessionData fetchRealSessionData() {
        RealSessionData out;
        // v1.27.1: env-var short-circuit for tests + CI. Bypasses the
        // `icmg context-budget --all-sessions` subprocess which on Win
        // dev machines with N×MB transcript JSONLs takes ~5min (NTFS
        // small-file I/O + CreateProcess overhead). Set to skip.
        const char* sk = std::getenv("ICMG_SAVINGS_NO_REAL_SESSIONS");
        if (sk && *sk && std::string(sk) != "0") return out;
        std::string bin;
#ifdef _WIN32
        char buf[1024]; DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
        if (n > 0) bin = buf;
#endif
        if (bin.empty()) {
            const char* e = std::getenv("ICMG_BIN");
            bin = e ? e : "icmg";
        }
        // v1.27.3 (Bug 1 fix): default ON for multi-user aggregate. Shared-
        // server installs (typical icmg deployment) had Active Users panel
        // showing only the env-var-setter's name. Set ICMG_SAVINGS_ALL_USERS=0
        // to opt-out (single-user view).
        const char* au = std::getenv("ICMG_SAVINGS_ALL_USERS");
        bool all_users = !au || !*au || std::string(au) != "0";
        std::string cmd = "\"" + bin + "\" context-budget --all-sessions"
                         + (all_users ? " --all-users" : "") + " --json 2>/dev/null";
        auto res = core::safeExecShell(cmd, false, 15000);
        if (res.exit_code != 0 || res.out.empty()) return out;
        const std::string& s = res.out;

        size_t sess_arr = s.find("\"sessions\":");
        size_t top_end  = (sess_arr == std::string::npos) ? s.size() : sess_arr;
        out.total_tokens  = parseIntAfter(s, "\"total_tokens\":", 0, top_end);
        out.session_count = (int)parseIntAfter(s, "\"session_count\":", 0, top_end);
        out.user_count    = (int)parseIntAfter(s, "\"user_count\":",    0, top_end);

        size_t agg_bs = s.find("\"by_source\":");
        if (agg_bs != std::string::npos && agg_bs < top_end) {
            size_t agg_obj = s.find('{', agg_bs);
            size_t agg_end = top_end;
            if (agg_obj != std::string::npos && agg_obj < agg_end) {
                out.tool_input  = sumSourceTokens(s, agg_obj, agg_end, "tool-input:", true);
                out.tool_output = sumSourceTokens(s, agg_obj, agg_end, "tool-output", false);
                out.thinking    = sumSourceTokens(s, agg_obj, agg_end, "thinking",    false);
                int64_t a = sumSourceTokens(s, agg_obj, agg_end, "user",      false);
                int64_t b = sumSourceTokens(s, agg_obj, agg_end, "assistant", false);
                int64_t c = sumSourceTokens(s, agg_obj, agg_end, "text",      false);
                int64_t d = sumSourceTokens(s, agg_obj, agg_end, "other",     false);
                out.text = a + b + c + d;
            }
        }

        if (sess_arr != std::string::npos) {
            size_t cursor = sess_arr;
            while (true) {
                size_t obj = s.find("{\"file\":\"", cursor);
                if (obj == std::string::npos) break;
                size_t obj_end = s.find("}}", obj);
                if (obj_end == std::string::npos) break;
                obj_end += 2;

                RealSessionRow r;
                size_t fp = obj + 9;
                size_t fe = s.find('"', fp);
                if (fe == std::string::npos) break;
                r.file  = s.substr(fp, fe - fp);
                // v1.9.0: extract optional "user" field after file.
                size_t up = s.find("\"user\":\"", fe);
                if (up != std::string::npos && up < obj_end) {
                    size_t us = up + 8;
                    size_t ue = s.find('"', us);
                    if (ue != std::string::npos && ue < obj_end) {
                        r.user = s.substr(us, ue - us);
                    }
                }
                r.mtime = parseIntAfter(s, "\"mtime\":",       fe, obj_end);
                r.total = parseIntAfter(s, "\"total_tokens\":", fe, obj_end);

                size_t row_bs = s.find("\"by_source\":", fe);
                if (row_bs != std::string::npos && row_bs < obj_end) {
                    size_t row_obj = s.find('{', row_bs);
                    if (row_obj != std::string::npos && row_obj < obj_end) {
                        r.tool_input  = sumSourceTokens(s, row_obj, obj_end, "tool-input:", true);
                        r.tool_output = sumSourceTokens(s, row_obj, obj_end, "tool-output", false);
                        r.thinking    = sumSourceTokens(s, row_obj, obj_end, "thinking",    false);
                        int64_t a = sumSourceTokens(s, row_obj, obj_end, "user",      false);
                        int64_t b = sumSourceTokens(s, row_obj, obj_end, "assistant", false);
                        int64_t c = sumSourceTokens(s, row_obj, obj_end, "text",      false);
                        int64_t d = sumSourceTokens(s, row_obj, obj_end, "other",     false);
                        r.text = a + b + c + d;
                    }
                }
                out.sessions.push_back(std::move(r));
                cursor = obj_end;
            }
        }
        return out;
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
</div>)HTML";

        // v1.5.0: aggregate REAL session tokens across all transcripts in
        // current project + emit per-session detail table with source breakdown.
        RealSessionData rsd = fetchRealSessionData();
        if (rsd.total_tokens > 0) {
            int64_t instrumented = tot.raw_tokens;
            int coverage_pct = rsd.total_tokens > 0
                                ? (int)(100 * instrumented / rsd.total_tokens) : 0;
            if (coverage_pct > 100) coverage_pct = 100;
            // v1.28.0 (Bug 2 fix): break "Outside coverage" into 2 honest
            // categories so user can act:
            //   - Conversation = user + assistant + plain text + other.
            //     Inherent to chat. Cannot be filtered by icmg by design.
            //   - Tool calls outside icmg = tool-input + tool-output not
            //     yet routed through icmg run / context / pack / fetch.
            //     FIXABLE — use icmg-first more strictly.
            int64_t conversation = rsd.text;
            int64_t tool_total   = rsd.tool_input + rsd.tool_output;
            int64_t tool_outside = tool_total > instrumented ? tool_total - instrumented : 0;
            os << R"HTML(<div style="background:#161b22;border:1px solid #30363d;border-radius:8px;padding:20px;margin-bottom:24px;display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:24px">
<div><div style="color:#8b949e;font-size:12px;text-transform:uppercase;letter-spacing:.05em;margin-bottom:6px">Real session tokens</div><div style="font-size:28px;font-weight:700;color:#58a6ff">)HTML"
               << humanTok(rsd.total_tokens)
               << R"HTML(</div><div style="color:#6e7681;font-size:11px;margin-top:4px">Sum across )HTML"
               << rsd.session_count
               << R"HTML( session(s) in this project</div></div>
<div><div style="color:#8b949e;font-size:12px;text-transform:uppercase;letter-spacing:.05em;margin-bottom:6px">Icmg-covered</div><div style="font-size:28px;font-weight:700;color:#3fb950">)HTML"
               << humanTok(instrumented) << " (" << coverage_pct
               << R"HTML(%)</div><div style="color:#6e7681;font-size:11px;margin-top:4px">Tracked in dashboard</div></div>
<div><div style="color:#8b949e;font-size:12px;text-transform:uppercase;letter-spacing:.05em;margin-bottom:6px">Conversation</div><div style="font-size:28px;font-weight:700;color:#8b949e">)HTML"
               << humanTok(conversation)
               << R"HTML(</div><div style="color:#6e7681;font-size:11px;margin-top:4px">User + assistant chat (inherent, uncoverable)</div></div>
<div><div style="color:#8b949e;font-size:12px;text-transform:uppercase;letter-spacing:.05em;margin-bottom:6px">Tool calls outside icmg</div><div style="font-size:28px;font-weight:700;color:#d29922">)HTML"
               << humanTok(tool_outside)
               << R"HTML(</div><div style="color:#6e7681;font-size:11px;margin-top:4px">Raw Read/Bash/MCP — fixable: route via icmg</div></div>
</div>)HTML";
            // v1.9.0: active-users panel (set ICMG_SAVINGS_ALL_USERS=1 for multi-user agg).
            if (rsd.user_count >= 1) {
                std::map<std::string,int64_t> per_user;
                for (auto& r : rsd.sessions) per_user[r.user.empty()?std::string("(unknown)"):r.user] += r.total;
                os << R"HTML(<div style="background:#161b22;border:1px solid #30363d;border-radius:8px;padding:16px 20px;margin-bottom:24px">
<div style="color:#8b949e;font-size:12px;text-transform:uppercase;letter-spacing:.05em;margin-bottom:8px;font-weight:500">Active users ()HTML"
                   << per_user.size()
                   << R"HTML()</div>
<table style="margin-top:8px"><thead><tr><th>User</th><th class="num">Sessions</th><th class="num">Real tokens</th></tr></thead><tbody>)HTML";
                std::map<std::string,int> sess_per_user;
                for (auto& r : rsd.sessions) sess_per_user[r.user.empty()?std::string("(unknown)"):r.user] += 1;
                for (auto& [u, t] : per_user) {
                    os << "<tr><td style=\"font-family:ui-monospace,monospace;font-size:12px;color:#c9d1d9\">"
                       << u << "</td><td class=\"num\">" << sess_per_user[u]
                       << "</td><td class=\"num\">" << humanTok(t) << "</td></tr>";
                }
                os << R"HTML(</tbody></table>
<div style="color:#6e7681;font-size:11px;margin-top:8px">Multi-user aggregate default ON (v1.27.3). Set <code>ICMG_SAVINGS_ALL_USERS=0</code> for single-user view.</div>
</div>)HTML";
            }

            // Detail table: per-session source breakdown.
            if (!rsd.sessions.empty()) {
                os << R"HTML(<details style="margin-bottom:24px;background:#161b22;border:1px solid #30363d;border-radius:8px;padding:16px 20px">
<summary style="cursor:pointer;font-size:13px;color:#8b949e;text-transform:uppercase;letter-spacing:.05em;font-weight:500">Per-session detail ()HTML"
                   << rsd.sessions.size()
                   << R"HTML( sessions, source breakdown)</summary>
<table style="margin-top:12px">
<thead><tr><th>User</th><th>Session</th><th>Date</th><th class="num">Total</th><th class="num">Text</th><th class="num">Tool in</th><th class="num">Tool out</th><th class="num">Thinking</th></tr></thead>
<tbody>)HTML";
                for (auto& r : rsd.sessions) {
                    char dbuf[24] = "";
                    std::time_t t = (std::time_t)r.mtime;
                    if (t > 0) {
                        std::tm tm_buf{};
#ifdef _WIN32
                        localtime_s(&tm_buf, &t);
#else
                        localtime_r(&t, &tm_buf);
#endif
                        std::strftime(dbuf, sizeof(dbuf), "%Y-%m-%d %H:%M", &tm_buf);
                    }
                    std::string label = r.file;
                    if (label.size() > 16) label = label.substr(0, 8) + "..." + label.substr(label.size() - 5);
                    std::string urow = r.user.empty() ? std::string("(unknown)") : r.user;
                    os << "<tr><td style=\"font-family:ui-monospace,monospace;font-size:12px;color:#c9d1d9\">"
                       << urow << "</td><td style=\"font-family:ui-monospace,monospace;font-size:12px;color:#8b949e\">"
                       << label << "</td><td>" << dbuf << "</td>"
                       << "<td class=\"num\">" << humanTok(r.total) << "</td>"
                       << "<td class=\"num\">" << humanTok(r.text) << "</td>"
                       << "<td class=\"num\">" << humanTok(r.tool_input) << "</td>"
                       << "<td class=\"num\">" << humanTok(r.tool_output) << "</td>"
                       << "<td class=\"num\">" << humanTok(r.thinking) << "</td></tr>";
                }
                os << R"HTML(</tbody></table></details>)HTML";
            }

            // Daily history: group by YYYY-MM-DD, newest first.
            std::map<std::string, int64_t, std::greater<std::string>> by_day_html;
            std::map<std::string, int> sess_per_day;
            for (auto& r : rsd.sessions) {
                if (r.mtime <= 0) continue;
                std::time_t t = (std::time_t)r.mtime;
                std::tm tm_buf{};
#ifdef _WIN32
                localtime_s(&tm_buf, &t);
#else
                localtime_r(&t, &tm_buf);
#endif
                char dbuf[16]; std::strftime(dbuf, sizeof(dbuf), "%Y-%m-%d", &tm_buf);
                by_day_html[dbuf] += r.total;
                ++sess_per_day[dbuf];
            }
            if (!by_day_html.empty()) {
                int64_t day_max = 0;
                for (auto& [_, v] : by_day_html) if (v > day_max) day_max = v;
                os << R"HTML(<details style="margin-bottom:24px;background:#161b22;border:1px solid #30363d;border-radius:8px;padding:16px 20px" open>
<summary style="cursor:pointer;font-size:13px;color:#8b949e;text-transform:uppercase;letter-spacing:.05em;font-weight:500">Daily real-token history ()HTML"
                   << by_day_html.size()
                   << R"HTML( days, newest first)</summary>
<table style="margin-top:12px">
<thead><tr><th>Date</th><th class="num">Sessions</th><th class="num">Real tokens</th><th>Trend</th></tr></thead>
<tbody>)HTML";
                int shown = 0;
                for (auto& [d, tok] : by_day_html) {
                    if (shown++ >= 30) break;
                    int bar_pct = day_max > 0 ? (int)(100 * tok / day_max) : 0;
                    os << "<tr><td style=\"font-family:ui-monospace,monospace;font-size:12px\">"
                       << d << "</td>"
                       << "<td class=\"num\">" << sess_per_day[d] << "</td>"
                       << "<td class=\"num\">" << humanTok(tok) << "</td>"
                       << "<td style=\"width:40%\"><div style=\"background:#21262d;border-radius:3px;height:10px;overflow:hidden\">"
                       << "<div style=\"height:100%;width:" << bar_pct
                       << "%;background:linear-gradient(90deg,#58a6ff,#3fb950)\"></div></div></td></tr>";
                }
                os << R"HTML(</tbody></table></details>)HTML";
            }
        }

        os << R"HTML(<div class="cards">)HTML";

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
        if (pack_recpt_.calls > 0)   emitRow(os, "Pack receipts",   pack_recpt_);
        if (strict_block_.calls > 0) emitRow(os, "Strict denials",  strict_block_);
        if (fetch_cache_.calls > 0)  emitRow(os, "Fetch cache",     fetch_cache_);
        if (image_cache_.calls > 0)  emitRow(os, "Image OCR cache", image_cache_);
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

        // v1.27.3 (Bug 3 fix): include pack_recpt + strict_denials + bfs in
        // daily chart so its sum matches the Total row. Previously omitted
        // → user saw Total=942K but daily chart only ~120K. Each strict
        // denial = 1500 tok (same heuristic as line 188); each BFS = 2000
        // (same as line 245). Pack receipts from token_receipts table.
        try {
            db.query("SELECT date(ts,'unixepoch'), "
                     "       COALESCE(SUM(CASE WHEN raw_tokens>0 "
                     "                          THEN raw_tokens - est_tokens "
                     "                          ELSE 0 END),0) "
                     "FROM token_receipts WHERE ts > ? "
                     "GROUP BY date(ts,'unixepoch')",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){
                         if (r.size() >= 2 && !r[0].empty()) {
                             try { by_day[r[0]] += std::stoll(r[1]); } catch (...) {}
                         }
                     });
        } catch (...) {}
        // Strict denials JSONL bucketed by day.
        {
            const char* home = std::getenv("USERPROFILE");
            if (!home) home = std::getenv("HOME");
            if (home) {
                std::filesystem::path log = std::filesystem::path(home)
                    / ".icmg" / "strict-denials.jsonl";
                if (std::filesystem::exists(log)) {
                    std::ifstream lf(log);
                    std::string line;
                    while (std::getline(lf, line)) {
                        auto p = line.find("\"ts\":");
                        if (p == std::string::npos) continue;
                        int64_t ts = 0;
                        try { ts = std::stoll(line.substr(p + 5)); } catch (...) { continue; }
                        if (ts < cutoff) continue;
                        time_t t = (time_t)ts;
                        std::tm tmb{};
#ifdef _WIN32
                        localtime_s(&tmb, &t);
#else
                        localtime_r(&t, &tmb);
#endif
                        char d[16];
                        std::strftime(d, sizeof(d), "%Y-%m-%d", &tmb);
                        by_day[d] += 1500;
                    }
                }
                std::filesystem::path bfslog = std::filesystem::path(home)
                    / ".icmg" / "bfs-queries.jsonl";
                if (std::filesystem::exists(bfslog)) {
                    std::ifstream lf(bfslog);
                    std::string line;
                    while (std::getline(lf, line)) {
                        auto p = line.find("\"ts\":");
                        if (p == std::string::npos) continue;
                        int64_t ts = 0;
                        try { ts = std::stoll(line.substr(p + 5)); } catch (...) { continue; }
                        if (ts < cutoff) continue;
                        time_t t = (time_t)ts;
                        std::tm tmb{};
#ifdef _WIN32
                        localtime_s(&tmb, &t);
#else
                        localtime_r(&t, &tmb);
#endif
                        char d[16];
                        std::strftime(d, sizeof(d), "%Y-%m-%d", &tmb);
                        by_day[d] += 2000;
                    }
                }
            }
        }

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
