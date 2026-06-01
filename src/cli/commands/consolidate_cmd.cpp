// Phase 26 T1: `icmg memory consolidate` — collapse near-dupes.
//
// With embedder: pairwise cosine ≥ 0.92 → keep highest-frequency, sum freq, soft-delete loser.
// Without embedder: Jaccard ≥ 0.85 fallback (existing similarity logic).
//
// --dry-run reports candidates without mutation.
// --topic-prefix scopes work to one prefix.
// --threshold N override (default 0.92 cosine, 0.85 Jaccard).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/global_db.hpp"
#include "../../imem/memory_store.hpp"
#include "../../embed/embedder.hpp"
#include "../../embed/embed_store.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <set>
#include <map>
#include <sstream>

namespace icmg::cli {

class MemoryConsolidateCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-consolidate"; }
    std::string description() const override { return "Collapse near-duplicate memory nodes"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg memory consolidate [options]\n\n"
            "Options:\n"
            "  --threshold N        Cosine 0..1 (default 0.92) or Jaccard fallback (default 0.85)\n"
            "  --topic-prefix S     Scope to topic prefix\n"
            "  --zone NAME          Scope to one zone (e.g. default)\n"
            "  --dry-run            Report candidates only\n"
            "  --json\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool dry      = hasFlag(args, "--dry-run");
        bool json_out = hasFlag(args, "--json");
        bool all_proj = hasFlag(args, "--all-projects");
        std::string prefix = flagValue(args, "--topic-prefix");
        std::string zone   = flagValue(args, "--zone");

        auto& cfg = core::Config::instance();
        if (all_proj) return runAll(cfg, dry, prefix);
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        auto embedder = embed::makeEmbedder();
        bool use_cosine = embedder && embedder->available();
        double threshold;
        try { threshold = std::stod(flagValue(args, "--threshold",
                                              use_cosine ? "0.92" : "0.85")); }
        catch (...) { threshold = use_cosine ? 0.92 : 0.85; }

        // Pull candidates.
        auto nodes = store.all();
        if (!prefix.empty()) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const imem::MemoryNode& n){ return n.topic.find(prefix) != 0; }),
                nodes.end());
        }
        if (!zone.empty()) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const imem::MemoryNode& n){ return n.zone != zone; }),
                nodes.end());
        }
        if (nodes.size() < 2) {
            std::cout << "consolidate: < 2 nodes to compare.\n";
            return 0;
        }

        // Embeddings if available.
        std::vector<std::vector<float>> vecs(nodes.size());
        if (use_cosine) {
            embed::EmbedStore es(db);
            std::vector<int64_t> ids;
            for (auto& n : nodes) ids.push_back(n.id);
            auto rows = es.getMany("memory", ids, embedder->dim());
            std::map<int64_t, std::vector<float>> by_id;
            for (auto& kv : rows) by_id.emplace(kv.first, std::move(kv.second));
            for (size_t i = 0; i < nodes.size(); ++i) {
                auto it = by_id.find(nodes[i].id);
                if (it != by_id.end()) vecs[i] = it->second;
            }
        }

        // Pairwise compare. O(n^2) — okay up to ~5k nodes.
        struct Merge { size_t keeper, loser; double sim; };
        std::vector<Merge> merges;
        std::set<size_t> consumed;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (consumed.count(i)) continue;
            for (size_t j = i + 1; j < nodes.size(); ++j) {
                if (consumed.count(j)) continue;
                double sim;
                if (use_cosine && !vecs[i].empty() && !vecs[j].empty()) {
                    sim = embed::cosine(vecs[i], vecs[j]);
                } else {
                    sim = jaccard(nodes[i].topic + " " + nodes[i].content,
                                  nodes[j].topic + " " + nodes[j].content);
                }
                if (sim >= threshold) {
                    size_t k = (nodes[i].frequency >= nodes[j].frequency) ? i : j;
                    size_t l = (k == i) ? j : i;
                    merges.push_back({k, l, sim});
                    consumed.insert(l);
                }
            }
        }

        if (json_out) {
            std::cout << "{\"merges\":" << merges.size()
                      << ",\"method\":\"" << (use_cosine ? "cosine" : "jaccard")
                      << "\",\"threshold\":" << threshold << "}\n";
        } else {
            std::cout << "Consolidate (" << (use_cosine ? "cosine" : "jaccard")
                      << " >= " << std::fixed << std::setprecision(2) << threshold
                      << "): " << merges.size() << " merge(s)" << (dry ? " [dry-run]" : "") << "\n";
        }
        if (dry || merges.empty()) return 0;

        for (auto& m : merges) {
            auto& keeper = nodes[m.keeper];
            auto& loser  = nodes[m.loser];
            // Sum frequency into keeper.
            db.run("UPDATE memory_nodes SET frequency = frequency + ?, "
                   "last_used = MAX(last_used, ?) WHERE id = ?",
                   {std::to_string(loser.frequency),
                    std::to_string(loser.last_used),
                    std::to_string(keeper.id)});
            // Soft-delete loser.
            db.run("UPDATE memory_nodes SET deleted_at = strftime('%s','now') "
                   "WHERE id = ?", {std::to_string(loser.id)});
        }
        std::cout << "  applied. " << merges.size() << " loser(s) soft-deleted; freq summed into keepers.\n";
        return 0;
    }

    int runAll(core::Config& cfg, bool dry, const std::string& prefix) {
        try {
            auto& gdb = core::GlobalDb::instance();
            gdb.init();
            auto projs = gdb.listProjects();
            if (projs.empty()) {
                std::cerr << "consolidate --all-projects: no registered projects\n";
                return 1;
            }
            int total_merges = 0;
            for (auto& p : projs) {
                if (p.db_path.empty()) continue;
                std::cout << "[" << p.name << "] " << p.db_path << "\n";
                try {
                    cfg.setProjectDbOverride(p.db_path);
                    // Re-call run() recursively without --all-projects flag.
                    std::vector<std::string> args;
                    if (dry) args.push_back("--dry-run");
                    if (!prefix.empty()) { args.push_back("--topic-prefix"); args.push_back(prefix); }
                    int ec = run(args);
                    (void)ec;  // count comes from individual report
                    cfg.clearProjectDbOverride();
                } catch (const std::exception& e) {
                    std::cerr << "  ! skipped: " << e.what() << "\n";
                    cfg.clearProjectDbOverride();
                }
                ++total_merges;
            }
            std::cout << "consolidate --all-projects: processed " << total_merges
                      << " project(s)" << (dry ? " [dry-run]" : "") << "\n";
        } catch (const std::exception& e) {
            std::cerr << "consolidate --all-projects: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

private:
    static double jaccard(const std::string& a, const std::string& b) {
        auto tok = [](const std::string& s) {
            std::set<std::string> out;
            std::istringstream ss(s);
            std::string t;
            while (ss >> t) {
                std::string l = t;
                for (auto& c : l) c = (char)::tolower((unsigned char)c);
                if (l.size() > 2) out.insert(l);
            }
            return out;
        };
        auto ta = tok(a), tb = tok(b);
        if (ta.empty() && tb.empty()) return 0.0;
        size_t inter = 0;
        for (auto& s : ta) if (tb.count(s)) ++inter;
        size_t uni = ta.size() + tb.size() - inter;
        return uni ? (double)inter / uni : 0.0;
    }
};

ICMG_REGISTER_COMMAND("memory-consolidate", MemoryConsolidateCommand);

} // namespace icmg::cli
