// `icmg find "<intent>"` -- one-shot multi-file intent search. Walks the project
// for source files, ranks them by relevance to the intent, and prints the top
// files each with only their relevant line windows (with line numbers). Collapses
// a Read -> Grep -> Read chain into a single turn.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../graph/graph_store.hpp"
#include "../../graph/graph_centrality.hpp"
#include "../find_slices.hpp"
#include "../find_name.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <chrono>
#include <cmath>

namespace icmg::cli {

namespace {
namespace fs = std::filesystem;

// Convert fs::path -> UTF-8 std::string without throwing on non-ACP names
// (.string() raises error 1113 for unmappable chars on Windows). Works for
// both C++17 (u8string -> std::string) and C++20 (u8string -> std::u8string).
inline std::string pathU8(const fs::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

bool isSkipDir(const std::string& name) {
    static const std::set<std::string> skip = {
        ".git", ".svn", ".hg", ".icmg", "node_modules", "third_party", "vendor",
        "dist", "build", "out", "target", ".vs", ".vscode", ".idea",
        "__pycache__", ".next", ".cache", "bin", "obj", "coverage",
        "build-msvc-full", "icmg-build"};
    if (!name.empty() && name[0] == '.' && name != ".") {
        if (skip.count(name)) return true;
    }
    return skip.count(name) > 0;
}

bool isSourceExt(const std::string& ext) {
    // Code-focused: prose docs (.md/.txt) are excluded so keyword-dense
    // markdown can't drown out the actual source for a code intent.
    static const std::set<std::string> ok = {
        ".cpp",".hpp",".h",".hh",".cc",".cxx",".c",".py",".js",".jsx",".ts",
        ".tsx",".go",".java",".rs",".cs",".php",".rb",".kt",".swift",".scala",
        ".lua",".sh",".ps1",".sql",".json",".yaml",".yml",".toml",
        ".html",".css",".vue",".svelte",".cmake"};
    return ok.count(ext) > 0;
}
}  // namespace

class FindCommand : public BaseCommand {
public:
    std::string name()        const override { return "find"; }
    std::string description() const override {
        return "Locate files fast: --name fuzzy filename search, or intent -> relevant code slices (fewer turns)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg find \"<intent>\" [options]\n\n"
            "Ranks project source files by relevance to the intent and prints the\n"
            "top files with only their relevant line windows -- the answer in one\n"
            "turn instead of a Read->Grep->Read chain.\n\n"
            "Options:\n"
            "  --name            Fuzzy-locate files by NAME only (no body read; fast)\n"
            "  --recent          With --name: rank newest-modified files first\n"
            "  --open            With --name: also print the top match's contents (locate+read, 1 turn)\n"
            "  --depends-on F    List all files that F transitively depends on (forward BFS)\n"
            "  --used-by F       List all files that transitively depend on F (reverse BFS)\n"
            "  --depth N         BFS max depth for --depends-on / --used-by (default 10)\n"
            "  --max-files N     Top files to show (default 5)\n"
            "  --ctx N           Context lines around each hit (default 4)\n"
            "  --max-bytes N     Cap total output (default 6000)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }

        // --depends-on / --used-by: graph-traversal mode
        if (hasFlag(args, "--depends-on"))
            return runTraverse(flagValue(args, "--depends-on", ""), false, args);
        if (hasFlag(args, "--used-by"))
            return runTraverse(flagValue(args, "--used-by", ""), true, args);

        // Join positional words into the intent; skip flags AND their values
        // (so `--max-files 3` does not leak "3" into the query).
        static const std::set<std::string> valFlags = {"--max-files", "--ctx", "--max-bytes"};
        std::string intent;
        for (size_t i = 0; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (a.empty()) continue;
            if (a[0] == '-') { if (valFlags.count(a)) ++i; continue; }  // skip flag (+ its value)
            if (!intent.empty()) intent += " ";
            intent += a;
        }
        if (intent.empty()) { usage(); return 1; }

        // --name: fast filename-only fuzzy locate (skips reading file bodies).
        if (hasFlag(args, "--name")) return runNameSearch(intent, args);

        int max_files = 5, ctx = 4;
        size_t max_bytes = 6000;
        try { max_files = std::stoi(flagValue(args, "--max-files", "5")); } catch (...) {}
        try { ctx = std::stoi(flagValue(args, "--ctx", "4")); } catch (...) {}
        try { max_bytes = (size_t)std::stoul(flagValue(args, "--max-bytes", "6000")); } catch (...) {}

        // Collect (relpath, body) for project source files; bounded.
        std::vector<std::pair<std::string, std::string>> files;
        std::unordered_map<std::string, double> ageSec;  // relpath -> age (seconds)
        const auto nowT = fs::file_time_type::clock::now();
        const size_t kMaxScan = 4000, kMaxFileBytes = 256 * 1024;
        std::error_code ec;
        fs::path base = fs::current_path(ec);
        size_t scanned = 0;
        auto it = fs::recursive_directory_iterator(
            base, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; it != end && scanned < kMaxScan; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            const fs::path& p = it->path();
            if (it->is_directory(ec)) {
                if (isSkipDir(p.filename().string())) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            std::string ext = p.extension().string();
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            if (!isSourceExt(ext)) continue;
            std::uintmax_t sz = it->file_size(ec);
            if (ec || sz == 0 || sz > kMaxFileBytes) { ec.clear(); continue; }
            std::ifstream f(p, std::ios::binary);
            if (!f) continue;
            std::ostringstream ss; ss << f.rdbuf();
            std::string rel = fs::relative(p, base, ec).string();
            if (ec || rel.empty()) { rel = p.string(); ec.clear(); }
            // Working-set recency: age in seconds (>=0) for the ranking boost.
            std::error_code tec;
            auto mt = fs::last_write_time(p, tec);
            if (!tec) {
                double age = std::chrono::duration<double>(nowT - mt).count();
                if (age < 0.0) age = 0.0;
                ageSec[rel] = age;
            }
            files.emplace_back(rel, ss.str());
            ++scanned;
        }

        auto hits = rankFileSlices(files, intent, ctx, max_files, /*maxWin*/3, &ageSec);

        // v2.8.2: PageRank blend -- boost hits by graph centrality.
        // Graph authority is a structural signal (high in-degree files = important).
        // Blend: score *= (1 + pr_alpha * pr_norm) where pr_norm in [0,1].
        // Graceful: if graph DB unavailable, hits are unmodified.
        if (!hits.empty() && !hasFlag(args, "--no-pr")) {
            try {
                core::Config& cfg = core::Config::instance();
                core::Db gdb(cfg.projectDbPath("."));
                graph::GraphStore gstore(gdb);
                auto nodes = gstore.all();
                std::vector<graph::GraphEdge> edges;
                for (auto& n : nodes) {
                    auto ef = gstore.edgesFrom(n.id);
                    edges.insert(edges.end(), ef.begin(), ef.end());
                }
                if (!nodes.empty()) {
                    auto pr = graph::pageRank(nodes, edges);
                    // Build relpath -> PR score map (normalise by max)
                    double pr_max = 0.0;
                    std::unordered_map<std::string, double> pr_map;
                    for (auto& n : nodes) {
                        auto it = pr.find(n.id);
                        double s = it != pr.end() ? it->second : 0.0;
                        // use the file path relative to cwd as key
                        std::string key = n.path;
                        // normalise path separators
                        for (auto& c : key) if (c == '\\') c = '/';
                        pr_map[key] = s;
                        if (s > pr_max) pr_max = s;
                    }
                    const double pr_blend = 0.3; // max 30% boost from PageRank
                    for (auto& h : hits) {
                        std::string hkey = h.file;
                        for (auto& c : hkey) if (c == '\\') c = '/';
                        auto it = pr_map.find(hkey);
                        if (it != pr_map.end() && pr_max > 0.0) {
                            double pr_norm = it->second / pr_max;
                            h.score *= (1.0 + pr_blend * pr_norm);
                        }
                    }
                    // Re-sort after blend
                    std::sort(hits.begin(), hits.end(),
                        [](const FileSlice& a, const FileSlice& b){ return a.score > b.score; });
                }
            } catch (...) { /* graph unavailable -- proceed without PR */ }
        }
        if (hits.empty()) {
            std::cout << "icmg find: no relevant lines for \"" << intent
                      << "\" (scanned " << scanned << " files)\n";
            return 0;
        }

        std::ostringstream out;
        out << "icmg find \"" << intent << "\" -- " << hits.size()
            << " file(s) (scanned " << scanned << "):\n";
        for (const auto& h : hits) {
            // re-split the body for this file to print numbered windows
            std::string body;
            for (const auto& pr : files) if (pr.first == h.file) { body = pr.second; break; }
            std::vector<std::string> lines;
            { std::istringstream is(body); std::string ln; while (std::getline(is, ln)) lines.push_back(ln); }
            out << "\n=== " << h.file << " (score " << h.score << ") ===\n";
            for (const auto& r : h.ranges) {
                out << "  lines " << r.start << "-" << r.end << ":\n";
                for (int n = r.start; n <= r.end && n <= (int)lines.size(); ++n)
                    out << std::setw(6) << n << "  " << lines[(size_t)n - 1] << "\n";
            }
        }
        std::string s = out.str();
        if (s.size() > max_bytes) {
            s.resize(max_bytes);
            s += "\n--- [output truncated; raise --max-bytes or narrow the intent] ---\n";
        }
        std::cout << s;
        return 0;
    }

private:
    // Graph-traversal mode: BFS forward (depends-on) or reverse (used-by).
    // reverse=false → what does `target` depend on?
    // reverse=true  → what depends on `target`?
    int runTraverse(const std::string& target, bool reverse,
                    const std::vector<std::string>& args) {
        if (target.empty()) {
            std::cerr << "icmg find: --"
                      << (reverse ? "used-by" : "depends-on")
                      << " requires a file path argument\n";
            return 1;
        }
        int depth = 10;
        try { depth = std::stoi(flagValue(args, "--depth", "10")); } catch (...) {}

        try {
            core::Config& cfg = core::Config::instance();
            const char* env_db = std::getenv("ICMG_PROJECT_DB");
            std::string db_path = (env_db && *env_db)
                ? std::string(env_db)
                : cfg.projectDbPath(".");
            core::Db db(db_path);
            graph::GraphStore gs(db);

            auto node = gs.getNode(target);
            if (!node) {
                std::cout << "icmg find: node not found in graph: " << target << "\n";
                return 1;
            }

            auto ids = gs.closure(node->id, {}, depth, reverse);
            if (ids.empty()) {
                std::cout << "icmg find --"
                          << (reverse ? "used-by" : "depends-on")
                          << " " << target << ": no results\n";
                return 0;
            }

            std::cout << "icmg find --"
                      << (reverse ? "used-by" : "depends-on")
                      << " " << target << " (" << ids.size() << "):\n";
            // Build id->path map from all nodes (bounded; graph is local)
            auto allNodes = gs.all();
            std::unordered_map<int64_t, std::string> id2path;
            for (auto& n : allNodes) id2path[n.id] = n.path;
            for (int64_t id : ids) {
                auto it = id2path.find(id);
                std::cout << "  " << (it != id2path.end() ? it->second : "(id:" + std::to_string(id) + ")") << "\n";
            }
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "icmg find traverse error: " << e.what() << "\n";
            return 1;
        }
    }

    // Fast path: walk the tree collecting RELATIVE PATHS only (no body read),
    // rank by filename similarity, print the best matches. ~10-50x faster than
    // the content search when you just need to locate a file by (partial) name.
    int runNameSearch(const std::string& query, const std::vector<std::string>& args) {
        int max_files = 20;
        try { max_files = std::stoi(flagValue(args, "--max-files", "20")); } catch (...) {}
        const bool recent = hasFlag(args, "--recent");
        const bool open   = hasFlag(args, "--open");
        size_t max_bytes = 8000;
        try { max_bytes = (size_t)std::stoul(flagValue(args, "--max-bytes", "8000")); } catch (...) {}

        std::vector<std::string> paths;
        std::unordered_map<std::string, long long> mtime;  // only filled when --recent
        const size_t kMaxScan = 20000;
        std::error_code ec;
        fs::path base = fs::current_path(ec);
        size_t scanned = 0;
        auto it = fs::recursive_directory_iterator(
            base, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; it != end && scanned < kMaxScan; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            const fs::path& p = it->path();
            if (it->is_directory(ec)) {
                if (isSkipDir(p.filename().string())) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            // .string() can throw error 1113 (unmappable char) on Windows for
            // non-ACP filenames; .u8string() yields UTF-8 and never throws.
            std::string rel;
            { std::error_code rec; auto rp = fs::relative(p, base, rec);
              rel = rec ? pathU8(p) : pathU8(rp); }
            if (rel.empty()) rel = pathU8(p);
            paths.push_back(rel);
            if (recent) {
                std::error_code tec;
                auto t = fs::last_write_time(p, tec);
                if (!tec) mtime[rel] = (long long)t.time_since_epoch().count();
            }
            ++scanned;
        }

        auto hits = rankFilenames(paths, query, max_files);
        if (hits.empty()) {
            std::cout << "icmg find --name: no file matching \"" << query
                      << "\" (scanned " << scanned << " files)\n";
            return 0;
        }
        if (recent) sortByRecency(hits, mtime);
        std::cout << "icmg find --name \"" << query << "\""
                  << (recent ? " (by recency)" : "") << " -- " << hits.size()
                  << " match(es) (scanned " << scanned << "):\n";
        for (const auto& h : hits)
            std::cout << "  " << h.path << "\n";

        // --open: print the contents of the best match inline (locate + read).
        if (open) {
            const std::string& top = hits.front().path;
            std::ifstream f(base / fs::u8path(top), std::ios::binary);
            if (f) {
                std::ostringstream ss; ss << f.rdbuf();
                std::cout << "\n=== " << top << " ===\n"
                          << numberLines(ss.str(), max_bytes);
            } else {
                std::cout << "\n(could not open " << top << " for --open)\n";
            }
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("find", FindCommand);

}  // namespace icmg::cli
