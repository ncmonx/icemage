// Phase 74 T5: `icmg cache` — inspect + manage hot-context cache.
//
// Subcommands:
//   stats [--window-sec N]   Hits / size / hit-rate over window (default 24h).
//   prune                    Drop expired rows.
//   clear [--cmd NAME]       Wipe all entries, or only for one cmd kind.
//   list [--cmd NAME] [--limit N]
//                            Show recent hot entries (cmd, hit_count, age).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/tool_call_cache.hpp"

#include <iomanip>
#include <iostream>
#include <string>

namespace icmg::cli {

class CacheCommand : public BaseCommand {
public:
    std::string name()        const override { return "cache"; }
    std::string description() const override {
        return "Hot-context cache (stats / prune / clear / list)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg cache <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  stats [--window-sec N]   Hits + hit-rate (default window 24h)\n"
            "  prune                    Drop expired rows\n"
            "  clear [--cmd NAME]       Wipe all, or only one cmd kind\n"
            "  list [--cmd NAME] [--limit N]\n"
            "                           Recent hot entries\n\n"
            "Disable cache for a single call: ICMG_NO_CACHE=1 icmg <cmd>\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        if (sub == "stats") return cmdStats(rest);
        if (sub == "prune") return cmdPrune(rest);
        if (sub == "clear") return cmdClear(rest);
        if (sub == "list")  return cmdList(rest);
        std::cerr << "icmg cache: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    int cmdStats(const std::vector<std::string>& args) {
        int window = 86400;
        try { window = std::stoi(flagValue(args, "--window-sec", "86400")); } catch (...) {}
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        core::ToolCallCache tcc(db);
        auto s = tcc.summary(window);
        std::cout << "icmg cache stats (window: " << window << "s)\n"
                  << "  total rows: " << s.total << "\n"
                  << "  hits:       " << s.hits << "\n"
                  << "  hit rate:   " << s.hit_rate_pct << "%\n";
        return 0;
    }

    int cmdPrune(const std::vector<std::string>&) {
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        core::ToolCallCache tcc(db);
        int n = tcc.prune();
        std::cout << "icmg cache prune: " << n << " expired row(s) removed\n";
        return 0;
    }

    int cmdClear(const std::vector<std::string>& args) {
        std::string only = flagValue(args, "--cmd");
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        if (only.empty()) {
            db.run("DELETE FROM tool_call_cache");
            std::cout << "icmg cache clear: all rows removed\n";
        } else {
            db.run("DELETE FROM tool_call_cache WHERE cmd = ?", {only});
            std::cout << "icmg cache clear: rows for cmd='" << only << "' removed\n";
        }
        return 0;
    }

    int cmdList(const std::vector<std::string>& args) {
        std::string only = flagValue(args, "--cmd");
        int lim = 20;
        try { lim = std::stoi(flagValue(args, "--limit", "20")); } catch (...) {}
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        std::string sql =
            "SELECT cmd, content_hash, hit_count, "
            "(strftime('%s','now') - created_at) age, "
            "(expires_at - strftime('%s','now')) ttl "
            "FROM tool_call_cache ";
        std::vector<std::string> params;
        if (!only.empty()) {
            sql += "WHERE cmd = ? ";
            params.push_back(only);
        }
        sql += "ORDER BY hit_count DESC, created_at DESC LIMIT " + std::to_string(lim);

        std::cout << "CMD          HASH(8)   HITS  AGE     TTL\n";
        std::cout << std::string(50, '-') << "\n";
        int n = 0;
        db.query(sql, params, [&](const core::Row& r){
            if (r.size() < 5) return;
            ++n;
            std::cout << std::left << std::setw(12) << r[0]
                      << " " << r[1].substr(0, 8)
                      << "  " << std::right << std::setw(4) << r[2]
                      << "  " << std::setw(6) << r[3] + "s"
                      << "  " << std::setw(5) << r[4] + "s" << "\n";
        });
        if (n == 0) std::cout << "  (empty)\n";
        std::cout << "Total: " << n << " row(s)\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("cache", CacheCommand);

} // namespace icmg::cli
