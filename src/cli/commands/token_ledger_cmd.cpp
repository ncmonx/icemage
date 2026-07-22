// `icmg token-ledger` — REAL Anthropic API token meter (Gap #3).
//
// The GUI (icemage-code AgentLoop) flushes one row per turn:
//   icmg token-ledger record --input N --output N --cache-read N
//                            --cache-creation N [--model M] [--session SID]
// `show` / `today` / `total` read it back; `icmg savings` also surfaces it as
// the "Real API tokens" section. Honest meter, not a proxy estimate.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../token_ledger.hpp"
#include "../model_pricing.hpp"
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class TokenLedgerCommand : public BaseCommand {
public:
    std::string name()        const override { return "token-ledger"; }
    std::string description() const override {
        return "Real Anthropic API token meter (input/output/cache + cost)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg token-ledger <action> [options]\n\n"
            "Actions:\n"
            "  record   Record one usage row (from an API response)\n"
            "    --input N           input_tokens\n"
            "    --output N          output_tokens\n"
            "    --cache-read N      cache_read_input_tokens\n"
            "    --cache-creation N  cache_creation_input_tokens\n"
            "    --model NAME        model id (for pricing)\n"
            "    --source S          recording app (default 'gui')\n"
            "    --session SID       session id\n"
            "  show     [--last N]       Print recent rows (default 20)\n"
            "  total    [--window Nd]    Aggregate within window (default 30d)\n"
            "  today                     Aggregate since local midnight\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage(); return 0;
        }
        std::string action = args[0];
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        if (action == "record") {
            TokenLedgerEntry e;
            e.session_id = flagValue(args, "--session");
            e.model      = flagValue(args, "--model");
            e.source     = flagValue(args, "--source", "gui");
            e.input_tokens          = parseLL(flagValue(args, "--input"));
            e.output_tokens         = parseLL(flagValue(args, "--output"));
            e.cache_read_tokens     = parseLL(flagValue(args, "--cache-read"));
            e.cache_creation_tokens = parseLL(flagValue(args, "--cache-creation"));
            bool wrote = recordTokenLedger(db, e);
            // Silent success (called per-turn by the GUI; no chatter).
            return wrote ? 0 : 0;
        }

        if (action == "show") {
            int limit = 20;
            try { limit = std::stoi(flagValue(args, "--last", "20")); } catch (...) {}
            ensureTokenLedger(db);
            std::cout << "Recent API token usage (last " << limit << "):\n"
                      << "  " << std::left
                      << std::setw(20) << "when"
                      << std::setw(8)  << "in"
                      << std::setw(8)  << "out"
                      << std::setw(9)  << "c-read"
                      << std::setw(9)  << "c-make"
                      << "model\n"
                      << "  " << std::string(64, '-') << "\n";
            db.query("SELECT ts, input_tokens, output_tokens, cache_read_tokens,"
                     " cache_creation_tokens, model FROM token_ledger"
                     " ORDER BY ts DESC LIMIT ?",
                     {std::to_string(limit)},
                     [](const core::Row& r) {
                         if (r.size() < 6) return;
                         char buf[24] = "";
                         try {
                             std::time_t t = (std::time_t)std::stoll(r[0]);
                             std::tm tm_buf{};
#ifdef _WIN32
                             localtime_s(&tm_buf, &t);
#else
                             localtime_r(&t, &tm_buf);
#endif
                             std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_buf);
                         } catch (...) {}
                         std::cout << "  " << std::left
                                   << std::setw(20) << buf
                                   << std::setw(8)  << r[1]
                                   << std::setw(8)  << r[2]
                                   << std::setw(9)  << r[3]
                                   << std::setw(9)  << r[4]
                                   << r[5] << "\n";
                     });
            return 0;
        }

        if (action == "total" || action == "today") {
            int window_days = 30;
            int64_t cutoff = 0;
            if (action == "today") {
                std::time_t now = std::time(nullptr);
                std::tm tm_buf{};
#ifdef _WIN32
                localtime_s(&tm_buf, &now);
#else
                localtime_r(&now, &tm_buf);
#endif
                tm_buf.tm_hour = 0; tm_buf.tm_min = 0; tm_buf.tm_sec = 0;
                cutoff = (int64_t)std::mktime(&tm_buf);
                window_days = 0;
            } else {
                std::string w = flagValue(args, "--window", "30d");
                if (!w.empty() && w.back() == 'd') w.pop_back();
                try { window_days = std::stoi(w); } catch (...) {}
            }

            TokenLedgerTotals t = (action == "today")
                ? aggregateSince(db, cutoff)
                : aggregateTokenLedger(db, window_days);

            std::string priceModel = flagValue(args, "--model");
            if (priceModel.empty()) { if (const char* e = std::getenv("ICMG_MODEL")) priceModel = e; }
            ModelRates rates = modelPricing(priceModel);
            // Cost: fresh input + cache-write at input rate; cache-read at ~10%;
            // output at output rate. Lower-bound honest estimate.
            double cost = (double)(t.input + t.cache_creation) * rates.in / 1'000'000.0
                        + (double)t.cache_read * rates.in * 0.1 / 1'000'000.0
                        + (double)t.output * rates.out / 1'000'000.0;

            const char* label = (action == "today") ? "today" : "window";
            std::cout << "Real API token usage (" << label << "):\n"
                      << "  rows            " << t.rows << "\n"
                      << "  input           " << t.input << "\n"
                      << "  output          " << t.output << "\n"
                      << "  cache-read      " << t.cache_read << "\n"
                      << "  cache-creation  " << t.cache_creation << "\n"
                      << "  -- total in     " << t.totalInput() << "\n"
                      << "  est. cost       $" << std::fixed << std::setprecision(4)
                      << cost << "\n";
            return 0;
        }

        // v2.20 research #5: cache-hit ratio + cost over the existing
        // token_ledger (extend, not a parallel table). `stats` reports the
        // cache-hit fraction (cache-read / total input) -- the dominant 2026
        // cost lever -- alongside token totals and an honest cost estimate.
        // `otel` emits the same as OpenTelemetry GenAI-style JSON (offline).
        if (action == "stats" || action == "otel") {
            int window_days = 30;
            std::string w = flagValue(args, "--window", "30d");
            if (!w.empty() && w.back() == 'd') w.pop_back();
            try { window_days = std::stoi(w); } catch (...) {}
            TokenLedgerTotals t = aggregateTokenLedger(db, window_days);
            double cacheHit = t.cacheHitRate();   // existing helper (0..1)

            // Honest, model-agnostic cost estimate: fresh input + cache-creation
            // at $3/Mtok, cache-read at ~10% of that, output at $15/Mtok
            // (Claude-class defaults; a rough meter, not billing truth).
            double cost = (double)(t.input + t.cache_creation) * 3.0 / 1'000'000.0
                        + (double)t.cache_read * 0.30 / 1'000'000.0
                        + (double)t.output * 15.0 / 1'000'000.0;

            if (action == "otel") {
                std::cout << "{\n"
                    << "  \"name\": \"gen_ai.client.window\",\n"
                    << "  \"attributes\": {\n"
                    << "    \"gen_ai.usage.input_tokens\": " << t.totalInput() << ",\n"
                    << "    \"gen_ai.usage.output_tokens\": " << t.output << ",\n"
                    << "    \"gen_ai.usage.cached_tokens\": " << t.cache_read << ",\n"
                    << "    \"icmg.cache_hit_ratio\": " << std::fixed << std::setprecision(4) << cacheHit << ",\n"
                    << "    \"icmg.est_cost_usd\": " << std::fixed << std::setprecision(4) << cost << ",\n"
                    << "    \"icmg.rows\": " << t.rows << "\n"
                    << "  }\n}\n";
                return 0;
            }

            std::cout << "Token ledger stats (window " << window_days << "d):\n"
                      << "  rows            " << t.rows << "\n"
                      << "  input (fresh)   " << t.input << "\n"
                      << "  cache-read      " << t.cache_read << "\n"
                      << "  cache-creation  " << t.cache_creation << "\n"
                      << "  output          " << t.output << "\n"
                      << "  cache-hit ratio " << std::fixed << std::setprecision(1)
                      << (cacheHit * 100.0) << "%\n"
                      << "  est. cost       $" << std::fixed << std::setprecision(4)
                      << cost << "\n";
            return 0;
        }

        std::cerr << "icmg token-ledger: unknown action '" << action << "'\n";
        usage();
        return 1;
    }

private:
    static int64_t parseLL(const std::string& s) {
        if (s.empty()) return 0;
        try { return std::stoll(s); } catch (...) { return 0; }
    }

    // Aggregate rows with ts >= cutoff (used by 'today').
    static TokenLedgerTotals aggregateSince(core::Db& db, int64_t cutoff) {
        TokenLedgerTotals t;
        ensureTokenLedger(db);
        db.query("SELECT COUNT(*), COALESCE(SUM(input_tokens),0),"
                 " COALESCE(SUM(output_tokens),0), COALESCE(SUM(cache_read_tokens),0),"
                 " COALESCE(SUM(cache_creation_tokens),0) FROM token_ledger WHERE ts >= ?",
                 {std::to_string(cutoff)},
                 [&](const core::Row& r) {
                     if (r.size() < 5) return;
                     try {
                         t.rows           = std::stoll(r[0]);
                         t.input          = std::stoll(r[1]);
                         t.output         = std::stoll(r[2]);
                         t.cache_read     = std::stoll(r[3]);
                         t.cache_creation = std::stoll(r[4]);
                     } catch (...) {}
                 });
        return t;
    }
};

ICMG_REGISTER_COMMAND("token-ledger", TokenLedgerCommand);

} // namespace icmg::cli
