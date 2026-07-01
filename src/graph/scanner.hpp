#pragma once
#include "graph_store.hpp"
#include "extractor/base_extractor.hpp"
#include "../wasm/wasm_extractor.hpp"
#include <string>
#include <vector>

namespace icmg::graph {

class Scanner {
public:
    struct Options {
        int                      max_depth    = 20;
        std::vector<std::string> include_langs;      // empty = all
        std::vector<std::string> ignore_dirs  = {
            // VCS
            ".git", ".svn", ".hg",
            // JS/TS
            "node_modules", "dist", ".cache", ".next", ".nuxt",
            // Python
            "__pycache__", ".venv", "venv", ".pytest_cache",
            // Rust
            "target",
            // C/C++/C#
            "build", "out", "bin", "obj",
            // .NET / Visual Studio
            ".vs", ".idea",
            // icmg own data
            ".icmg",
            // Misc
            "coverage", ".nyc_output", "vendor"
        };
        bool                     skip_stale   = true; // skip if hash unchanged
        bool                     resolve_edges = true; // run edge resolution after scan
        bool                     gitignore    = true;  // A9: respect .gitignore
        // 2026-06-14: incremental xref. When true, the post-scan class
        // cross-reference pass only re-reads the files this scan changed
        // (updatedPaths()) instead of EVERY graph node (~7000 disk reads).
        // `graph update` sets this; full `graph scan` leaves it false so the
        // whole-graph xref still runs.
        bool                     incremental_xref = false;
    };

    explicit Scanner(GraphStore& store);

    // Returns number of files scanned/updated
    int scan(const std::string& root);
    int scan(const std::string& root, const Options& opts);

    // Paths of the files actually (re)written during the most recent scan().
    // Lets callers sync ONLY changed files to memory instead of re-walking the
    // whole graph (the `icmg graph update` mem-sync bottleneck). Canonical paths
    // matching graph_node.path. Reset at the start of each scan().
    const std::vector<std::string>& updatedPaths() const { return updated_paths_; }

private:
    GraphStore& store_;
    std::vector<std::string> updated_paths_;
    // WASM extractors registered in the persona DB, loaded once per scan().
    // Empty when none registered / no persona DB (the common case = no overhead).
    std::vector<icmg::wasm::WasmExtractor> wasm_extractors_;

    std::string md5File(const std::string& path) const;
    std::string detectLang(const std::string& ext) const;
    BaseExtractor* getExtractor(const std::string& lang) const;

    // A9: gitignore loading (supports *, **, ?, !negation, trailing /, leading /)
    struct GitIgnore {
        struct Pattern {
            std::string raw;       // original (post-strip) pattern text
            bool        negate;    // '!' prefix → un-ignore
            bool        dir_only;  // trailing '/' → directories only
            bool        anchored;  // leading '/'  → relative to root
        };
        std::vector<Pattern> patterns;
        void load(const std::string& path);
        bool matches(const std::string& relpath) const;

    private:
        static bool globMatch(const std::string& pattern,
                              const std::string& path);
    };
};

} // namespace icmg::graph
