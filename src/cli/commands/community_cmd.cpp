// Phase 26 T6: `icmg graph communities` — Louvain cluster detection.
// File-level only (parent_id IS NULL). --apply-as-zones writes glob rules.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../graph/community.hpp"
#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <map>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace icmg::cli {

class CommunityCommand : public BaseCommand {
private:
    struct N { int64_t id; std::string path; };
public:
    std::string name()        const override { return "graph-communities"; }
    std::string description() const override { return "Louvain cluster detection on file graph"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg graph communities [options]\n\n"
            "Options:\n"
            "  --min-size N        Filter clusters smaller than N (default 2)\n"
            "  --apply-as-zones    Write zone_config rules (opt-in)\n"
            "  --json\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool apply    = hasFlag(args, "--apply-as-zones");
        bool json_out = hasFlag(args, "--json");
        int min_size = 2;
        try { min_size = std::stoi(flagValue(args, "--min-size", "2")); } catch (...) {}

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        // Pull file nodes + index by id.
        std::vector<N> nodes;
        std::unordered_map<int64_t, int> id_to_idx;
        db.query("SELECT id, path FROM graph_nodes WHERE kind='file' OR kind IS NULL "
                 "ORDER BY id", {},
                 [&](const core::Row& r){
                     if (r.size() < 2) return;
                     try {
                         N x; x.id = std::stoll(r[0]); x.path = r[1];
                         id_to_idx[x.id] = (int)nodes.size();
                         nodes.push_back(std::move(x));
                     } catch (...) {}
                 });

        // Build adjacency (file-to-file edges only).
        graph::AdjList adj(nodes.size());
        db.query("SELECT src, dst, weight FROM graph_edges", {},
                 [&](const core::Row& r){
                     if (r.size() < 3) return;
                     try {
                         int64_t s = std::stoll(r[0]);
                         int64_t d = std::stoll(r[1]);
                         double w = r[2].empty() ? 1.0 : std::stod(r[2]);
                         auto si = id_to_idx.find(s);
                         auto di = id_to_idx.find(d);
                         if (si == id_to_idx.end() || di == id_to_idx.end()) return;
                         if (si->second == di->second) return;
                         adj[si->second].push_back({di->second, w});
                         adj[di->second].push_back({si->second, w});
                     } catch (...) {}
                 });

        if (nodes.empty()) {
            std::cout << "No files in graph. Run `icmg graph scan` first.\n";
            return 1;
        }

        auto res = graph::louvain(adj);

        // Bucket nodes per cluster + pick majority dir.
        std::map<int, std::vector<int>> buckets;
        for (size_t i = 0; i < nodes.size(); ++i) buckets[res.cluster_id[i]].push_back((int)i);

        std::vector<std::pair<int, std::vector<int>>> sorted(buckets.begin(), buckets.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](auto& a, auto& b){ return a.second.size() > b.second.size(); });

        if (json_out) {
            std::cout << "{\"modularity\":" << res.modularity << ",\"clusters\":[";
            bool first = true;
            for (auto& [cid, idxs] : sorted) {
                if ((int)idxs.size() < min_size) continue;
                if (!first) std::cout << ",";
                first = false;
                std::cout << "{\"id\":" << cid << ",\"size\":" << idxs.size()
                          << ",\"label\":\"" << dirLabel(nodes, idxs) << "\"}";
            }
            std::cout << "]}\n";
        } else {
            std::cout << "Communities: " << res.cluster_count
                      << " clusters, modularity=" << std::fixed
                      << std::setprecision(3) << res.modularity << "\n";
            int shown = 0;
            for (auto& [cid, idxs] : sorted) {
                if ((int)idxs.size() < min_size) continue;
                std::string label = dirLabel(nodes, idxs);
                std::cout << "Cluster #" << (shown + 1) << " (" << label
                          << ", " << idxs.size() << " files)\n";
                int n = 0;
                for (int i : idxs) {
                    if (++n > 5) { std::cout << "  ... +" << (idxs.size() - 5) << " more\n"; break; }
                    std::cout << "  " << nodes[i].path << "\n";
                }
                ++shown;
            }
        }

        if (apply) {
            applyAsZones(db, nodes, sorted, min_size);
        }
        return 0;
    }

private:
    std::string dirLabel(const std::vector<N>& nodes,
                                  const std::vector<int>& idxs) {
        std::map<std::string, int> tally;
        for (int i : idxs) {
            try {
                fs::path p = nodes[i].path;
                std::string parent = p.parent_path().filename().string();
                if (!parent.empty()) ++tally[parent];
            } catch (...) {}
        }
        std::string best; int bc = 0;
        for (auto& [d, c] : tally) if (c > bc) { bc = c; best = d; }
        return best.empty() ? "mixed" : best;
    }
    void applyAsZones(core::Db& db, const std::vector<N>& nodes,
                      const std::vector<std::pair<int, std::vector<int>>>& clusters,
                      int min_size) {
        try {
            db.run("CREATE TABLE IF NOT EXISTS zone_config("
                   " glob TEXT PRIMARY KEY, zone TEXT NOT NULL,"
                   " priority INTEGER NOT NULL DEFAULT 100,"
                   " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
        } catch (...) {}
        int written = 0;
        for (auto& [cid, idxs] : clusters) {
            if ((int)idxs.size() < min_size) continue;
            std::string label = dirLabel(nodes, idxs);
            if (label == "mixed") continue;
            std::string glob = "**/" + label + "/**";
            try {
                db.run("INSERT OR REPLACE INTO zone_config(glob, zone, priority) "
                       "VALUES(?, ?, 50)", {glob, label});
                ++written;
            } catch (...) {}
        }
        std::cout << "  --apply-as-zones: wrote " << written << " zone rule(s)\n";
    }
};

ICMG_REGISTER_COMMAND("graph-communities", CommunityCommand);

} // namespace icmg::cli
