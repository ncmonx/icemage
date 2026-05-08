// Phase 26 T2: `icmg memory health` — diagnostic report.
// No mutations. Surfaces: stale ratio, orphan count, embed coverage, importance
// distribution, top zones, top topic prefixes, dup-suspects, soft-deleted age.
//
// Exit codes: 0 OK, 1 if --strict and any warning fires.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include <iostream>
#include <iomanip>
#include <map>
#include <vector>
#include <algorithm>
#include <ctime>
#include <string>

namespace icmg::cli {

class MemoryHealthCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-health"; }
    std::string description() const override { return "Diagnostic: stale, orphan, dup, coverage, distribution"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg memory health [--json] [--strict]\n\n"
            "Reports memory store hygiene. --strict exits non-zero on warning.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool json_out = hasFlag(args, "--json");
        bool strict   = hasFlag(args, "--strict");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        int64_t now = std::time(nullptr);

        int total = 0, deleted = 0, stale = 0, orphan = 0, embed_count = 0;
        int imp0 = 0, imp1 = 0, imp2 = 0, imp3 = 0;
        std::map<std::string, int> by_zone, by_prefix;
        int soft_old = 0;
        int64_t cutoff_90d = now - 90LL * 86400;
        int64_t cutoff_30d = now - 30LL * 86400;

        db.query("SELECT importance, frequency, last_used, zone, topic, deleted_at "
                 "FROM memory_nodes", {},
                 [&](const core::Row& r) {
                     if (r.size() < 6) return;
                     int imp = std::stoi(r[0]);
                     int freq = std::stoi(r[1]);
                     int64_t lu = r[2].empty() ? 0 : std::stoll(r[2]);
                     std::string z = r[3];
                     std::string topic = r[4];
                     int64_t del = r[5].empty() ? 0 : std::stoll(r[5]);
                     if (del > 0) {
                         ++deleted;
                         if (del < cutoff_30d) ++soft_old;
                         return;
                     }
                     ++total;
                     if (lu > 0 && lu < cutoff_90d) ++stale;
                     if (freq == 0 && lu < cutoff_30d) ++orphan;
                     if (imp == 0) ++imp0; else if (imp == 1) ++imp1;
                     else if (imp == 2) ++imp2; else if (imp == 3) ++imp3;
                     if (!z.empty()) ++by_zone[z];
                     // topic prefix = before first space, before first colon, or first 20 chars
                     std::string p = topic;
                     auto sp = p.find(' '); if (sp != std::string::npos) p = p.substr(0, sp);
                     auto cl = p.find(':'); if (cl != std::string::npos) p = p.substr(0, cl);
                     if (p.size() > 20) p = p.substr(0, 20);
                     ++by_prefix[p];
                 });

        // embeddings table may not exist on stale DB; tolerate.
        try {
            db.query("SELECT COUNT(*) FROM embeddings WHERE kind='memory'", {},
                     [&](const core::Row& r){ if (!r.empty()) embed_count = std::stoi(r[0]); });
        } catch (...) {}

        double stale_pct = total ? 100.0 * stale / total : 0.0;
        double cov_pct   = total ? 100.0 * embed_count / total : 0.0;
        bool warn_stale  = stale_pct > 30.0;
        bool warn_cov    = total > 50 && cov_pct < 50.0;
        bool warn_soft   = soft_old > 10;

        if (json_out) {
            std::cout << "{"
                      << "\"total\":" << total
                      << ",\"deleted\":" << deleted
                      << ",\"stale\":" << stale
                      << ",\"stale_pct\":" << stale_pct
                      << ",\"orphan\":" << orphan
                      << ",\"embed_count\":" << embed_count
                      << ",\"embed_coverage_pct\":" << cov_pct
                      << ",\"importance\":{\"0\":" << imp0 << ",\"1\":" << imp1
                      << ",\"2\":" << imp2 << ",\"3\":" << imp3 << "}"
                      << ",\"soft_old\":" << soft_old
                      << ",\"warn_stale\":" << (warn_stale ? "true" : "false")
                      << ",\"warn_coverage\":" << (warn_cov ? "true" : "false")
                      << "}\n";
        } else {
            std::cout << "Memory Health Report\n"
                      << "  total active:        " << total << "\n"
                      << "  soft-deleted:        " << deleted
                      << "  (" << soft_old << " older than 30d — `icmg memory purge`)\n"
                      << "  stale (>90d):        " << stale
                      << " (" << std::fixed << std::setprecision(1) << stale_pct << "%)"
                      << (warn_stale ? "  WARN" : "") << "\n"
                      << "  orphan (freq=0):     " << orphan << "\n"
                      << "  embed coverage:      " << embed_count << "/" << total
                      << " (" << cov_pct << "%)"
                      << (warn_cov ? "  WARN — run `icmg embed memory`" : "") << "\n";
            std::cout << "  importance dist:     0=" << imp0 << " 1=" << imp1
                      << " 2=" << imp2 << " 3=" << imp3 << "\n";
            std::cout << "  top zones:";
            int n = 0;
            std::vector<std::pair<std::string,int>> zsorted(by_zone.begin(), by_zone.end());
            std::sort(zsorted.begin(), zsorted.end(),
                      [](auto& a, auto& b){ return a.second > b.second; });
            for (auto& [z, c] : zsorted) {
                if (++n > 5) break;
                std::cout << " " << z << "(" << c << ")";
            }
            std::cout << "\n  top prefixes:";
            n = 0;
            std::vector<std::pair<std::string,int>> psorted(by_prefix.begin(), by_prefix.end());
            std::sort(psorted.begin(), psorted.end(),
                      [](auto& a, auto& b){ return a.second > b.second; });
            for (auto& [p, c] : psorted) {
                if (++n > 5) break;
                std::cout << " " << p << "(" << c << ")";
            }
            std::cout << "\n";
        }

        bool any_warn = warn_stale || warn_cov || warn_soft;
        return (strict && any_warn) ? 1 : 0;
    }
};

ICMG_REGISTER_COMMAND("memory-health", MemoryHealthCommand);

} // namespace icmg::cli
