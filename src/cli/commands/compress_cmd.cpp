// Phase 39 T1: `icmg compress` — semantic prompt compression.
//
// Reads stdin or file, emits compressed text + reversible glossary preface.
// Default lossless (round-trip exact). --aggressive adds filler-strip.

#include "../base_command.hpp"
#include "../../core/stdin_util.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/tool_call_cache.hpp"
#include "../../compress/compressor.hpp"
#include "../../compress/glossary_store.hpp"

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace icmg::cli {

class CompressCommand : public BaseCommand {
public:
    std::string name()        const override { return "compress"; }
    std::string description() const override {
        return "Semantic prompt compression with reversible glossary";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg compress [options] [<file>]\n"
            "       cat input.txt | icmg compress\n\n"
            "Options:\n"
            "  --aggressive          Strip boilerplate filler (lossy)\n"
            "  --threshold N         Skip if est-tokens < N (default 8000)\n"
            "  --kind <ext>          Hint content kind (e.g., .log, .md, .cs)\n"
            "  --force               Compress even if shouldCompress() says no\n"
            "  --stats               Print 30-day telemetry summary, exit\n"
            "  --json                Machine-readable summary\n"
            "  -o <file>             Write to file instead of stdout\n"
            "  --no-cache            Bypass tool-call cache (force recompute)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }

        if (hasFlag(args, "--stats")) return printStats();

        // Phase 73: respect ~/.icmg/config.json `compress.threshold` and
        // `compress.mode` (lossless|aggressive). CLI flags override config.
        // Closes upstream report: hardcoded 8000 default ignored config.
        compress::CompressOptions opts;
        auto& cfg = core::Config::instance();
        std::string cfg_mode = cfg.getString("compress.mode", "");
        if (cfg_mode == "aggressive") opts.mode = compress::Mode::Aggressive;
        int cfg_threshold = cfg.getInt("compress.threshold", 0);
        if (cfg_threshold > 0) opts.threshold_tok = cfg_threshold;
        if (hasFlag(args, "--aggressive")) opts.mode = compress::Mode::Aggressive;
        try {
            std::string t = flagValue(args, "--threshold");
            if (!t.empty()) opts.threshold_tok = std::stoi(t);
        } catch (...) {}
        std::string kind = flagValue(args, "--kind");
        bool force      = hasFlag(args, "--force");
        bool json_out   = hasFlag(args, "--json");
        std::string out_path = flagValue(args, "-o");

        // Read input (last positional arg or stdin).
        std::string input = readInput(args);
        if (input.empty()) {
            std::cerr << "icmg compress: empty input\n";
            return 1;
        }

        if (force) opts.threshold_tok = 0;

        // Phase 74 T5: hot-context cache — same input + same opts within TTL → hit.
        // Saves recompression cost when Claude re-checks file mid-task.
        bool no_cache = hasFlag(args, "--no-cache") || std::getenv("ICMG_NO_CACHE");
        std::string cache_args =
            "thr=" + std::to_string(opts.threshold_tok)
          + "|mode=" + (opts.mode == compress::Mode::Aggressive ? "agg" : "loss")
          + "|kind=" + kind
          + "|in_sz=" + std::to_string(input.size())
          + "|in=" + input;  // FNV-1a inside makeKey hashes the whole thing
        std::optional<std::string> cached_text;
        compress::CompressResult r;
        bool cache_hit = false;
        if (!no_cache) {
            try {
                auto& cfg = core::Config::instance();
                core::Db db(cfg.projectDbPath("."));
                core::ToolCallCache tcc(db);
                auto opt = tcc.lookup("compress", cache_args);
                if (opt) {
                    r.text = *opt;
                    r.skipped = false;
                    r.bytes_in = (int)input.size();
                    r.bytes_out = (int)r.text.size();
                    r.tok_in = r.bytes_in / 4;
                    r.tok_out = r.bytes_out / 4;
                    r.elapsed_ms = 0;
                    cache_hit = true;
                }
            } catch (...) {}
        }
        if (!cache_hit) {
            compress::Compressor c(opts);
            r = c.compress(input, kind);
        }

        // Persist glossary + telemetry + cache (best-effort).
        try {
            auto& cfg = core::Config::instance();
            core::Db db(cfg.projectDbPath("."));
            compress::GlossaryStore store(db);
            if (!cache_hit && !r.skipped) store.save(r.content_hash, r.glossary);
            store.recordTelemetry("compress", r.bytes_in, r.bytes_out,
                                   r.tok_in, r.tok_out, r.elapsed_ms,
                                   cache_hit ? "cache-hit"
                                             : (r.skipped ? "skipped"
                                                          : (opts.mode == compress::Mode::Aggressive
                                                             ? "aggressive" : "lossless")));
            if (!cache_hit && !no_cache && !r.skipped) {
                core::ToolCallCache tcc(db);
                tcc.store("compress", cache_args, r.text, /*ttl_sec*/ 1800);
            }
        } catch (...) { /* db unavailable: still print result */ }

        // Output.
        std::ostream* os = &std::cout;
        std::ofstream of;
        if (!out_path.empty()) {
            of.open(out_path, std::ios::binary);
            if (!of) { std::cerr << "icmg compress: open " << out_path << " failed\n"; return 2; }
            os = &of;
        }

        if (json_out) {
            *os << "{\"tok_in\":" << r.tok_in
                << ",\"tok_out\":" << r.tok_out
                << ",\"bytes_in\":" << r.bytes_in
                << ",\"bytes_out\":" << r.bytes_out
                << ",\"elapsed_ms\":" << r.elapsed_ms
                << ",\"skipped\":" << (r.skipped ? "true" : "false")
                << ",\"skip_reason\":\"" << r.skip_reason << "\""
                << ",\"hash\":\"" << r.content_hash << "\"}\n";
        } else {
            *os << r.text;
        }

        // Stderr telemetry line for human eyes.
        if (r.skipped) {
            std::cerr << "[compress] SKIPPED: " << r.skip_reason
                      << " (" << r.tok_in << " tok)\n";
        } else {
            int saved_pct = r.tok_in > 0 ? (100 - (100 * r.tok_out / r.tok_in)) : 0;
            std::cerr << "[compress] " << r.tok_in << "→" << r.tok_out
                      << " tok (" << saved_pct << "% saved) in "
                      << r.elapsed_ms << "ms\n";
        }
        return 0;
    }

private:
    static std::string readInput(const std::vector<std::string>& args) {
        // Last non-flag positional treated as filename.
        std::string path;
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a == "-o" || a == "--threshold" || a == "--kind") { ++i; continue; }
            if (!a.empty() && a[0] == '-') continue;
            path = a;
        }
        if (!path.empty()) {
            std::ifstream f(path, std::ios::binary);
            if (!f) return {};
            std::ostringstream ss; ss << f.rdbuf();
            return ss.str();
        }
        // Stdin
        std::ostringstream ss;
        ss.str(core::slurpStdinSafe());
        return ss.str();
    }

    int printStats() {
        try {
            auto& cfg = core::Config::instance();
            core::Db db(cfg.projectDbPath("."));
            compress::GlossaryStore store(db);
            auto s = store.summary();
            int saved_pct = s.tok_in > 0 ? (int)(100 - (100 * s.tok_out / s.tok_in)) : 0;
            std::cout << "Compression telemetry (last 30d):\n"
                      << "  calls:    " << s.calls << "\n"
                      << "  tok in:   " << s.tok_in << "\n"
                      << "  tok out:  " << s.tok_out << "\n"
                      << "  saved:    " << saved_pct << "%\n"
                      << "  total ms: " << s.ms << "\n";
        } catch (const std::exception& e) {
            std::cerr << "icmg compress --stats: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("compress", CompressCommand);

} // namespace icmg::cli
