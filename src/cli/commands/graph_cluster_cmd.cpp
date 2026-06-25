// v1.55 Sub-D D7: `icmg graph cluster` — Leiden community detection CLI.
//
// Phase 1 (this commit): reads an undirected weighted edge list (one edge per
// line: "u v [w]") from --input <file> or stdin, runs Leiden, emits JSON to
// stdout. Node ids in input may be arbitrary integers — they are remapped
// to 0..N-1 internally and remapped back in the output.
//
// Phase 2 (TODO v1.56): wire directly to GraphStore. Walk graph_edges
// (or symbol_calls), build LeidenEdge list, write cluster_id back to
// graph_nodes via a migration.
//
// Usage:
//   icmg graph cluster --algorithm leiden [--input <file>] [--resolution 1.0]
//                      [--max-iter 10] [--seed 42] [--no-refine] [--json]

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../graph/leiden.hpp"
#include "../../core/db.hpp"
#include "../../core/config.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdlib>

namespace icmg::cli {

namespace {

struct Args {
    std::string algorithm = "leiden";
    std::string input;
    double resolution = 1.0;
    int    max_iter   = 10;
    uint64_t seed     = 42ULL;
    bool   refine     = true;
    bool   json       = false;
    bool   project    = false;  // pull edges from GraphStore
    int    top_n      = 0;      // 0 = all clusters
    bool   help       = false;
};

static Args parse(const std::vector<std::string>& argv) {
    Args a;
    for (size_t i = 0; i < argv.size(); ++i) {
        const std::string& s = argv[i];
        auto next = [&](const std::string& key) -> std::string {
            if (i + 1 >= argv.size()) {
                std::cerr << "[graph cluster] missing value for " << key << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if      (s == "--help" || s == "-h") a.help = true;
        else if (s == "--algorithm")  a.algorithm  = next(s);
        else if (s == "--input")      a.input      = next(s);
        else if (s == "--resolution") a.resolution = std::stod(next(s));
        else if (s == "--max-iter")   a.max_iter   = std::stoi(next(s));
        else if (s == "--seed")       a.seed       = (uint64_t)std::stoull(next(s));
        else if (s == "--no-refine")  a.refine     = false;
        else if (s == "--json")       a.json       = true;
        else if (s == "--project")    a.project    = true;
        else if (s == "--top-n")      a.top_n      = std::stoi(next(s));
    }
    return a;
}

static std::vector<icmg::graph::LeidenEdge>
readEdges(std::istream& in,
          std::unordered_map<long long, int>& id_to_idx,
          std::vector<long long>& idx_to_id) {
    std::vector<icmg::graph::LeidenEdge> edges;
    std::string line;
    while (std::getline(in, line)) {
        // skip comments / empty
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos || line[p] == '#') continue;
        std::istringstream ls(line);
        long long u, v;
        double w = 1.0;
        if (!(ls >> u >> v)) continue;
        ls >> w;  // optional
        auto map_id = [&](long long id) -> int {
            auto it = id_to_idx.find(id);
            if (it != id_to_idx.end()) return it->second;
            int idx = (int)idx_to_id.size();
            id_to_idx[id] = idx;
            idx_to_id.push_back(id);
            return idx;
        };
        icmg::graph::LeidenEdge e;
        e.u = map_id(u);
        e.v = map_id(v);
        e.w = (w > 0.0) ? w : 1.0;
        edges.push_back(e);
    }
    return edges;
}

class GraphClusterCmd : public BaseCommand {
public:
    std::string name() const override { return "graph-cluster"; }
    std::string description() const override {
        return "Leiden community detection. --project clusters the project graph directly.";
    }

    // Pull edges from GraphStore, run Leiden, print file-grouped clusters.
    int runProject(const Args& a) {
        const char* env_db = std::getenv("ICMG_PROJECT_DB");
        std::string db_path;
        if (env_db && *env_db) {
            db_path = env_db;
        } else {
            auto& cfg = icmg::core::Config::instance();
            db_path = cfg.projectDbOverride().empty()
                ? cfg.projectDbPath(".")
                : cfg.projectDbOverride();
        }
        if (db_path.empty()) {
            std::cerr << "[graph cluster] no project DB found; run `icmg init` first\n";
            return 1;
        }

        icmg::core::Db db(db_path);

        // Load file nodes: id -> path
        std::unordered_map<long long, std::string> id_to_path;
        db.query("SELECT id, path FROM graph_nodes WHERE kind IS NULL OR kind='file'",
                 {},
                 [&](const icmg::core::Row& row) {
                     id_to_path[std::stoll(row[0])] = row[1];
                 });

        if (id_to_path.empty()) {
            if (a.json)
                std::cout << "{\"num_nodes\":0,\"num_clusters\":0,"
                             "\"modularity\":0,\"passes\":0,\"clusters\":[]}\n";
            else
                std::cout << "# graph-cluster --project: no file nodes found\n";
            return 0;
        }

        // Load edges between file nodes only
        std::unordered_map<long long, int> id_to_idx;
        std::vector<long long>             idx_to_id;
        std::vector<icmg::graph::LeidenEdge> edges;

        auto map_id = [&](long long id) -> int {
            auto it = id_to_idx.find(id);
            if (it != id_to_idx.end()) return it->second;
            int idx = (int)idx_to_id.size();
            id_to_idx[id] = idx;
            idx_to_id.push_back(id);
            return idx;
        };

        db.query("SELECT src, dst FROM graph_edges", {},
                 [&](const icmg::core::Row& row) {
                     long long u = std::stoll(row[0]), v = std::stoll(row[1]);
                     if (!id_to_path.count(u) || !id_to_path.count(v)) return;
                     icmg::graph::LeidenEdge e;
                     e.u = map_id(u); e.v = map_id(v); e.w = 1.0;
                     edges.push_back(e);
                 });

        // Add isolated file nodes not reached via edges
        for (auto& [id, path] : id_to_path) {
            if (!id_to_idx.count(id)) map_id(id);
        }

        int N = (int)idx_to_id.size();

        icmg::graph::LeidenOptions opts;
        opts.resolution = a.resolution;
        opts.max_iter   = a.max_iter;
        opts.seed       = a.seed;
        opts.refine     = a.refine;

        auto R = icmg::graph::leidenCluster(N, edges, opts);

        // Group files by cluster, sort by size desc
        std::map<int, std::vector<std::string>> cluster_map;
        for (int i = 0; i < N; ++i)
            cluster_map[R.cluster[i]].push_back(id_to_path[idx_to_id[i]]);

        std::vector<std::pair<int,std::vector<std::string>>> sorted(
            cluster_map.begin(), cluster_map.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& x, const auto& y){ return x.second.size() > y.second.size(); });

        int show = (a.top_n > 0)
            ? std::min(a.top_n, (int)sorted.size())
            : (int)sorted.size();

        if (a.json) {
            std::cout << "{\"num_nodes\":" << N
                      << ",\"num_clusters\":" << R.num_clusters
                      << ",\"modularity\":" << R.modularity
                      << ",\"passes\":" << R.passes
                      << ",\"clusters\":[";
            for (int c = 0; c < show; ++c) {
                if (c) std::cout << ",";
                auto& [cid, files] = sorted[c];
                std::cout << "{\"id\":" << cid << ",\"size\":" << files.size()
                          << ",\"files\":[";
                for (size_t f = 0; f < files.size(); ++f) {
                    if (f) std::cout << ",";
                    std::cout << "\"" << files[f] << "\"";
                }
                std::cout << "]}";
            }
            std::cout << "]}\n";
        } else {
            std::cout << "# leiden: N=" << N
                      << " K=" << R.num_clusters
                      << " Q=" << R.modularity
                      << " passes=" << R.passes << "\n\n";
            for (int c = 0; c < show; ++c) {
                auto& [cid, files] = sorted[c];
                std::cout << "Cluster " << cid << " (" << files.size() << " files):\n";
                for (auto& f : files) std::cout << "  " << f << "\n";
                std::cout << "\n";
            }
        }
        return 0;
    }

    int run(const std::vector<std::string>& argv) override {
        Args a = parse(argv);

        if (a.help) {
            std::cout <<
                "Usage: icmg graph-cluster [options]\n\n"
                "Options:\n"
                "  --project            Pull edges from project GraphStore\n"
                "  --input <file>       Edge list file (u v [w] per line)\n"
                "  --json               Emit JSON output\n"
                "  --top-n N            Show only top N largest clusters\n"
                "  --algorithm leiden   Algorithm (only leiden supported)\n"
                "  --resolution R       Leiden resolution (default 1.0)\n"
                "  --max-iter N         Max passes (default 10)\n"
                "  --seed N             RNG seed (default 42)\n"
                "  --no-refine          Skip refinement phase\n"
                "  --help               Show this help\n\n"
                "Examples:\n"
                "  icmg graph-cluster --project            # cluster project files\n"
                "  icmg graph-cluster --project --json     # JSON output\n"
                "  icmg graph-cluster --project --top-n 5 # top 5 clusters\n"
                "  icmg graph-cluster --input edges.txt   # from edge list file\n";
            return 0;
        }

        if (a.project) return runProject(a);

        if (a.algorithm != "leiden") {
            std::cerr << "[graph cluster] unsupported algorithm: " << a.algorithm
                      << " (only 'leiden' for now)\n";
            return 2;
        }

        std::unordered_map<long long, int> id_to_idx;
        std::vector<long long> idx_to_id;
        std::vector<icmg::graph::LeidenEdge> edges;
        if (a.input.empty()) {
            edges = readEdges(std::cin, id_to_idx, idx_to_id);
        } else {
            std::ifstream f(a.input);
            if (!f) {
                std::cerr << "[graph cluster] cannot open --input " << a.input << "\n";
                return 2;
            }
            edges = readEdges(f, id_to_idx, idx_to_id);
        }

        int N = (int)idx_to_id.size();
        if (N == 0) {
            std::cerr << "[graph cluster] empty input — nothing to cluster\n";
            return 1;
        }

        icmg::graph::LeidenOptions opts;
        opts.resolution = a.resolution;
        opts.max_iter   = a.max_iter;
        opts.seed       = a.seed;
        opts.refine     = a.refine;

        auto R = icmg::graph::leidenCluster(N, edges, opts);

        if (a.json) {
            std::cout << "{\"num_nodes\":" << N
                      << ",\"num_clusters\":" << R.num_clusters
                      << ",\"modularity\":" << R.modularity
                      << ",\"passes\":" << R.passes
                      << ",\"assignments\":[";
            for (int i = 0; i < N; ++i) {
                if (i) std::cout << ",";
                std::cout << "{\"id\":" << idx_to_id[i]
                          << ",\"cluster\":" << R.cluster[i] << "}";
            }
            std::cout << "]}\n";
        } else {
            std::cout << "# leiden: N=" << N
                      << " K=" << R.num_clusters
                      << " Q=" << R.modularity
                      << " passes=" << R.passes << "\n";
            for (int i = 0; i < N; ++i) {
                std::cout << idx_to_id[i] << "\t" << R.cluster[i] << "\n";
            }
        }
        return 0;
    }
};

struct _GraphClusterReg {
    _GraphClusterReg() {
        auto& reg = icmg::core::Registry<BaseCommand>::instance();
        reg.reg("graph-cluster", []() -> std::unique_ptr<BaseCommand> {
            return std::make_unique<GraphClusterCmd>();
        });
    }
} _gc_inst;

} // namespace
} // namespace icmg::cli
