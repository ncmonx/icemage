// v1.1.0 Task 1+2: `icmg bench` — perf baseline + regression detector.
//
// Subcommands:
//   bench recall <query>  Measure semantic-recall latency (p50/p95/p99)
//   bench compress        Measure compress throughput (bytes_in/out + ratio)
//   bench hooks           Measure hook event latencies (Stop/PreCompact/...)
//   bench all             Run everything, emit JSON
//
// Output: human table by default; `--json` emits machine-parseable record.

#include "../base_command.hpp"
#include "../../core/stdin_util.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/hooks/runners.hpp"
#include "../../compress/compressor.hpp"
#include "../../imem/memory_store.hpp"
#include "../bench_savings.hpp"
#include "../../core/token_budget.hpp"
#include <filesystem>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

namespace {

using clk = std::chrono::steady_clock;
using ms_t = std::chrono::microseconds;

inline int64_t elapsed_us(clk::time_point t0) {
    return std::chrono::duration_cast<ms_t>(clk::now() - t0).count();
}

struct Stats {
    int64_t p50_us = 0;
    int64_t p95_us = 0;
    int64_t p99_us = 0;
    int64_t mean_us = 0;
    int     n = 0;
};

static Stats compute_stats(std::vector<int64_t>& samples) {
    Stats s;
    if (samples.empty()) return s;
    std::sort(samples.begin(), samples.end());
    s.n = (int)samples.size();
    auto pick = [&](double q) {
        size_t i = (size_t)((s.n - 1) * q);
        return samples[i];
    };
    s.p50_us = pick(0.50);
    s.p95_us = pick(0.95);
    s.p99_us = pick(0.99);
    int64_t sum = 0;
    for (auto x : samples) sum += x;
    s.mean_us = sum / s.n;
    return s;
}

static void print_stats_row(std::ostream& os, const std::string& label, const Stats& s) {
    os << "  " << label
       << "  n=" << s.n
       << "  p50=" << (s.p50_us / 1000) << "ms"
       << "  p95=" << (s.p95_us / 1000) << "ms"
       << "  p99=" << (s.p99_us / 1000) << "ms"
       << "\n";
}

static nlohmann::json stats_to_json(const Stats& s) {
    nlohmann::json j;
    j["n"]       = s.n;
    j["p50_ms"]  = s.p50_us / 1000.0;
    j["p95_ms"]  = s.p95_us / 1000.0;
    j["p99_ms"]  = s.p99_us / 1000.0;
    j["mean_ms"] = s.mean_us / 1000.0;
    return j;
}

// ---- bench recall -----------------------------------------------------------

static Stats bench_recall(const std::string& query, int n) {
    std::vector<int64_t> samples;
    samples.reserve(n);
    try {
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);
        for (int i = 0; i < n; ++i) {
            auto t0 = clk::now();
            auto rows = mem.recall(query, 10);
            (void)rows;
            samples.push_back(elapsed_us(t0));
        }
    } catch (...) {}
    return compute_stats(samples);
}

// ---- bench compress ---------------------------------------------------------

struct CompressBench {
    Stats stats;
    int   bytes_in  = 0;
    int   bytes_out = 0;
    double ratio    = 0.0;
};

static CompressBench bench_compress(const std::string& input, int n) {
    CompressBench out;
    if (input.empty()) return out;
    std::vector<int64_t> samples;
    samples.reserve(n);
    for (int i = 0; i < n; ++i) {
        compress::CompressOptions opts;
        opts.threshold_tok = 256;
        compress::Compressor c(opts);
        auto t0 = clk::now();
        auto r = c.compress(input);
        samples.push_back(elapsed_us(t0));
        if (i == 0) {
            out.bytes_in  = r.bytes_in;
            out.bytes_out = r.bytes_out;
            out.ratio     = out.bytes_in > 0
                          ? (double)out.bytes_out / (double)out.bytes_in
                          : 0.0;
        }
    }
    out.stats = compute_stats(samples);
    return out;
}

// ---- bench hooks ------------------------------------------------------------

struct HookBench {
    Stats stop;
    Stats precompact;
    Stats posttool_read;
};

static HookBench bench_hooks(int n) {
    HookBench out;
    std::string stop_in        = R"({"transcript":"x","session_id":"bench"})";
    std::string precompact_in  = R"({"transcript":""})";
    std::string post_in        = R"({"tool_response":{"content":"short"}})";

    auto bench_one = [&](const std::string& tag, const std::string& in) {
        std::vector<int64_t> samples;
        samples.reserve(n);
        for (int i = 0; i < n; ++i) {
            auto t0 = clk::now();
            if      (tag == "stop")          (void)core::hooks::runStopHook(in);
            else if (tag == "precompact")    (void)core::hooks::runPreCompactHook(in);
            else if (tag == "posttool_read") (void)core::hooks::runPostToolUseReadHook(in);
            samples.push_back(elapsed_us(t0));
        }
        return compute_stats(samples);
    };

    out.stop          = bench_one("stop",          stop_in);
    out.precompact    = bench_one("precompact",    precompact_in);
    out.posttool_read = bench_one("posttool_read", post_in);
    return out;
}

} // namespace

class BenchCommand : public BaseCommand {
public:
    std::string name()        const override { return "bench"; }
    std::string description() const override {
        return "Measure perf baseline (recall / compress / hooks)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg bench <action> [options]\n\n"
            "Actions:\n"
            "  recall <query>   Semantic recall latency (default --n 20)\n"
            "  compress         Compress throughput (reads stdin)\n"
            "  hooks            Hook event latencies (Stop/PreCompact/PostToolUseRead)\n"
            "  savings          Token read-savings on THIS repo (reproducible; --sample N)\n"
            "  all              Run all + emit JSON\n\n"
            "Options:\n"
            "  --n N            Trial count (default 20)\n"
            "  --json           Emit single JSON record (machine-parseable)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];
        int n = 20;
        try { n = std::stoi(flagValue(args, "--n", "20")); } catch (...) {}
        if (n < 1) n = 1;
        if (n > 200) n = 200;
        bool json_out = hasFlag(args, "--json") || action == "all";

        nlohmann::json out;
        out["icmg"]    = "bench";
        out["n"]       = n;

        if (action == "savings") {
            int sample = 0;
            try { sample = std::stoi(flagValue(args, "--sample", "0")); } catch (...) {}
            namespace fs = std::filesystem;
            static const std::set<std::string> skipDir = {
                ".git",".icmg","node_modules","third_party","vendor","dist","build",
                "out","target",".vs","build-msvc-full","icmg-build","__pycache__"};
            static const std::set<std::string> ext = {
                ".cpp",".hpp",".h",".hh",".cc",".cxx",".c",".py",".js",".jsx",".ts",".tsx",
                ".go",".java",".rs",".cs",".php",".rb",".kt",".kts",".swift",".scala",".lua",".sql",".sh"};
            std::vector<long long> toks;
            std::error_code ec;
            fs::path root = fs::current_path(ec);
            auto it = fs::recursive_directory_iterator(
                root, fs::directory_options::skip_permission_denied, ec);
            fs::recursive_directory_iterator dend;
            for (; it != dend; it.increment(ec)) {
                if (ec) { ec.clear(); continue; }
                if (it->is_directory(ec)) {
                    if (skipDir.count(it->path().filename().string())) it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file(ec)) continue;
                std::string e = it->path().extension().string();
                for (auto& c : e) c = (char)std::tolower((unsigned char)c);
                if (!ext.count(e)) continue;
                std::uintmax_t sz = it->file_size(ec);
                if (ec || sz == 0 || sz > 1024 * 1024) { ec.clear(); continue; }
                toks.push_back((long long)core::estimateTokens((std::size_t)sz));
                if (sample > 0 && (int)toks.size() >= sample) break;
            }
            long long cap = (long long)core::estimateTokens((std::size_t)4096);
            auto rs = benchReadSavings(toks, cap);
            if (json_out) {
                nlohmann::json j;
                j["files"] = rs.files; j["naive_tokens"] = rs.naiveTokens;
                j["icmg_tokens"] = rs.icmgTokens; j["pct_saved"] = rs.pctSaved;
                j["cap_tokens"] = cap;
                std::cout << j.dump() << "\n";
            } else {
                std::cout << "icmg bench savings -- reading this repo via `icmg context` vs raw Read:\n"
                          << "  files scanned:     " << rs.files << "\n"
                          << "  naive (full read): " << rs.naiveTokens << " tok\n"
                          << "  icmg (capped):     " << rs.icmgTokens << " tok\n"
                          << "  saved:             " << rs.pctSaved << "%  (cap ~" << cap << " tok/file)\n"
                          << "  note: icmg context caps each read; --for/--lines slices save further.\n";
            }
            return 0;
        }
        if (action == "recall") {
            std::string q;
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i][0] == '-') continue;
                if (!q.empty()) q += " ";
                q += args[i];
            }
            if (q.empty()) {
                std::cerr << "icmg bench recall: query required\n";
                return 1;
            }
            auto s = bench_recall(q, n);
            if (json_out) {
                out["recall"] = stats_to_json(s);
                std::cout << out.dump(2) << "\n";
            } else {
                std::cout << "bench recall — query=\"" << q << "\"\n";
                print_stats_row(std::cout, "recall", s);
            }
            return 0;
        }

        if (action == "compress") {
            std::ostringstream buf;
            buf.str(core::slurpStdinSafe());
            std::string in = buf.str();
            auto cb = bench_compress(in, n);
            if (json_out) {
                out["compress"] = stats_to_json(cb.stats);
                out["compress"]["bytes_in"]  = cb.bytes_in;
                out["compress"]["bytes_out"] = cb.bytes_out;
                out["compress"]["ratio"]     = cb.ratio;
                std::cout << out.dump(2) << "\n";
            } else {
                std::cout << "bench compress — " << cb.bytes_in << " -> " << cb.bytes_out
                          << "B (ratio " << cb.ratio << ")\n";
                print_stats_row(std::cout, "compress", cb.stats);
            }
            return 0;
        }

        if (action == "hooks") {
            auto h = bench_hooks(n);
            if (json_out) {
                out["hooks"]["stop"]          = stats_to_json(h.stop);
                out["hooks"]["precompact"]    = stats_to_json(h.precompact);
                out["hooks"]["posttool_read"] = stats_to_json(h.posttool_read);
                std::cout << out.dump(2) << "\n";
            } else {
                std::cout << "bench hooks (n=" << n << ")\n";
                print_stats_row(std::cout, "stop         ", h.stop);
                print_stats_row(std::cout, "precompact   ", h.precompact);
                print_stats_row(std::cout, "posttool_read", h.posttool_read);
            }
            return 0;
        }

        if (action == "all") {
            auto h = bench_hooks(n);
            out["hooks"]["stop"]          = stats_to_json(h.stop);
            out["hooks"]["precompact"]    = stats_to_json(h.precompact);
            out["hooks"]["posttool_read"] = stats_to_json(h.posttool_read);

            auto rs = bench_recall("test query", n);
            out["recall"] = stats_to_json(rs);

            std::string sample(2048, 'a');
            auto cb = bench_compress(sample, n);
            out["compress"] = stats_to_json(cb.stats);
            out["compress"]["bytes_in"]  = cb.bytes_in;
            out["compress"]["bytes_out"] = cb.bytes_out;
            out["compress"]["ratio"]     = cb.ratio;

            std::cout << out.dump(2) << "\n";
            return 0;
        }

        std::cerr << "icmg bench: unknown action '" << action << "'\n";
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("bench", BenchCommand);

} // namespace icmg::cli
