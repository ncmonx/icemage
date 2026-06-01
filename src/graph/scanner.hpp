#pragma once
#include "graph_store.hpp"
#include "extractor/base_extractor.hpp"
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
    };

    explicit Scanner(GraphStore& store);

    // Returns number of files scanned/updated
    int scan(const std::string& root);
    int scan(const std::string& root, const Options& opts);

private:
    GraphStore& store_;

    std::string md5File(const std::string& path) const;
    std::string detectLang(const std::string& ext) const;
    BaseExtractor* getExtractor(const std::string& lang) const;

    // A9: gitignore loading
    struct GitIgnore {
        std::vector<std::string> patterns;
        void load(const std::string& path);
        bool matches(const std::string& relpath) const;
    };
};

} // namespace icmg::graph
