#include "scanner.hpp"
#include "extractor/cpp_extractor.hpp"
#include "../core/registry.hpp"
#include "../core/zone_resolver.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <tuple>
#include <nlohmann/json.hpp>

// MD5 via simple streaming (enough for staleness check)
// We use a lightweight FNV-1a 64-bit hash as "hash" (not true MD5,
// but sufficient for file change detection).
#include <cstdint>

namespace fs = std::filesystem;

namespace icmg::graph {

// FNV-1a 64 as file hash (fast, no external deps)
static std::string hashFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    uint64_t hash = 14695981039346656037ULL;
    char buf[4096];
    while (f.read(buf, sizeof(buf)) || f.gcount()) {
        for (std::streamsize i = 0; i < f.gcount(); ++i) {
            hash ^= (uint8_t)buf[i];
            hash *= 1099511628211ULL;
        }
    }
    char out[17];
    snprintf(out, sizeof(out), "%016llx", (unsigned long long)hash);
    return std::string(out);
}

// Ext → lang name
static const struct { const char* ext; const char* lang; } EXT_MAP[] = {
    {".cpp","cpp"}, {".cxx","cpp"}, {".cc","cpp"}, {".c","cpp"},
    {".hpp","cpp"}, {".hxx","cpp"}, {".h","cpp"},
    {".py","python"}, {".pyw","python"},
    {".js","js"}, {".jsx","js"}, {".ts","js"}, {".tsx","js"}, {".mjs","js"},
    {".go","go"},
    {".rs","rust"},
    {".java","java"}, {".kt","java"}, {".kts","java"},
    {".cs","csharp"}, {".csx","csharp"},
    {".php","php"}, {".php5","php"}, {".phtml","php"},
};

Scanner::Scanner(GraphStore& store) : store_(store) {}

std::string Scanner::md5File(const std::string& path) const {
    return hashFile(path);
}

std::string Scanner::detectLang(const std::string& ext) const {
    for (auto& e : EXT_MAP) {
        if (ext == e.ext) return e.lang;
    }
    return "generic";
}

BaseExtractor* Scanner::getExtractor(const std::string& lang) const {
    // Try registered extractor via Registry<graph::BaseExtractor>
    auto& reg = core::Registry<graph::BaseExtractor>::instance();
    if (reg.has(lang)) return reg.create(lang).release();
    return nullptr;  // caller falls back to generic
}

// A9: simple .gitignore parser
void Scanner::GitIgnore::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        // Strip trailing whitespace
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.pop_back();
        if (!line.empty()) patterns.push_back(line);
    }
}

bool Scanner::GitIgnore::matches(const std::string& relpath) const {
    for (auto& pat : patterns) {
        // Simple glob: if pat ends with /, match directory prefix
        // Otherwise match anywhere in path
        std::string p = pat;
        if (!p.empty() && p[0] == '/') p = p.substr(1);
        bool dir_only = (!p.empty() && p.back() == '/');
        if (dir_only) p.pop_back();

        // Wildcard: * matches any segment
        if (p.find('*') != std::string::npos) {
            // Convert to simple match: only support *.ext and dir/*
            if (p.rfind("*.", 0) == 0) {
                std::string suffix = p.substr(1); // e.g. ".pyc"
                if (relpath.size() >= suffix.size() &&
                    relpath.substr(relpath.size() - suffix.size()) == suffix) return true;
            }
        } else {
            // Literal: match as path component.
            // Guard against unsigned underflow: relpath.size() - p.size() - 1 wraps
            // when p.size() >= relpath.size(), producing a false npos==npos match.
            bool tail_match = (p.size() < relpath.size()) &&
                              (relpath.rfind("/" + p) == relpath.size() - p.size() - 1);
            if (relpath == p || relpath.find(p + "/") != std::string::npos ||
                relpath.find(p + "\\") != std::string::npos || tail_match) return true;
        }
    }
    return false;
}

// Build JSON symbols string from ExtractResult
static std::string buildSymbols(const ExtractResult& r) {
    nlohmann::json j;
    j["imports"]    = r.imports;
    j["classes"]    = r.classes;
    j["functions"]  = r.functions;
    if (!r.tables.empty())      j["tables"]     = r.tables;
    if (!r.namespaces.empty())  j["namespaces"] = r.namespaces;
    return j.dump();
}

extern BaseExtractor* makeGenericExtractor();

int Scanner::scan(const std::string& root) {
    return scan(root, Options{});
}

int Scanner::scan(const std::string& root, const Options& opts) {
    // A9: load gitignore
    GitIgnore gi;
    if (opts.gitignore) gi.load(root + "/.gitignore");

    // Phase 17: zone resolver — auto-tag scanned files by path glob.
    icmg::core::ZoneResolver zoner(store_.db());

    fs::path root_path(root);
    int updated = 0;
    int max_file_size = 2 * 1024 * 1024; // skip files > 2MB

    auto* generic = makeGenericExtractor();

    // Pass 1: upsert all nodes, collect (src_id, src_path, import_name) for Pass 2 resolution
    // Tuple: (src_node_id, src_file_path, import_name_string)
    std::vector<std::tuple<int64_t,std::string,std::string>> pending;

    // Recursive walk
    std::function<void(const fs::path&, int)> walk = [&](const fs::path& dir, int depth) {
        if (depth > opts.max_depth) return;
        std::error_code iter_ec;
        for (auto& entry : fs::directory_iterator(dir, iter_ec)) {
            if (iter_ec) { iter_ec.clear(); continue; }
            std::string name = entry.path().filename().string();

            {
                std::error_code is_dir_ec;
                if (entry.is_directory(is_dir_ec)) {
                    // Check ignore_dirs
                    bool skip = false;
                    for (auto& ig : opts.ignore_dirs) {
                        if (name == ig) { skip = true; break; }
                    }
                    // Check gitignore
                    if (!skip && opts.gitignore) {
                        std::error_code rel_ec;
                        std::string rel = fs::relative(entry.path(), root_path, rel_ec).string();
                        if (!rel_ec && gi.matches(rel)) skip = true;
                    }
                    if (!skip) walk(entry.path(), depth + 1);
                    continue;
                }
            }

            {
                std::error_code is_reg_ec;
                if (!entry.is_regular_file(is_reg_ec)) continue;
            }

            std::string ext = entry.path().extension().string();
            std::string lang = detectLang(ext);

            // Filter by lang if specified
            if (!opts.include_langs.empty()) {
                bool found = false;
                for (auto& l : opts.include_langs) if (l == lang) { found = true; break; }
                if (!found) continue;
            }

            // File size guard
            std::error_code fsz_ec;
            auto fsz = entry.file_size(fsz_ec);
            if (fsz_ec || fsz > (uintmax_t)max_file_size) continue;

            // Normalize to canonical absolute path to prevent duplicate nodes
            // when scanning from different working directories (e.g. "." vs "src").
            std::error_code canon_ec;
            auto canon_path = fs::weakly_canonical(entry.path(), canon_ec);
            std::string fpath = canon_ec ? entry.path().string() : canon_path.string();
            std::string hash  = hashFile(fpath);

            // Skip if not stale
            if (opts.skip_stale && !store_.isStale(fpath, hash)) continue;

            // Read file
            std::ifstream f(fpath, std::ios::binary);
            if (!f) continue;
            std::ostringstream buf;
            buf << f.rdbuf();
            std::string content = buf.str();

            // Get extractor
            BaseExtractor* ext_ptr = getExtractor(lang);
            bool own_ext = (ext_ptr != nullptr);
            if (!ext_ptr) ext_ptr = generic;

            ExtractResult result = ext_ptr->extract(fpath, content);
            if (own_ext) delete ext_ptr;

            // Resolve zone (relative path for cleaner glob matching).
            std::error_code rel_ec;
            auto rel_for_zone = fs::relative(entry.path(), root_path, rel_ec);
            std::string zone_path = rel_ec ? fpath : rel_for_zone.string();

            // Build node
            GraphNode node;
            node.path       = fpath;
            node.lang       = lang;
            node.context    = result.context.substr(0, 500);
            node.symbols    = buildSymbols(result);
            node.size_bytes = (int64_t)fsz;
            node.file_hash  = hash;
            node.zone       = zoner.resolveForPath(zone_path);

            int64_t nodeId = store_.upsertNode(node);

            // Collect imports for Pass 2 resolution (don't insert edges yet)
            for (auto& imp : result.imports) {
                pending.emplace_back(nodeId, fpath, imp);
            }

            ++updated;
        }
    };

    walk(root_path, 0);

    // Pass 2: A7 — resolve imports to node IDs and insert edges
    if (opts.resolve_edges && !pending.empty()) {
        store_.resolveAndInsertEdges(pending);
    }

    // Strategy 4: class cross-reference — always run after scan, even if no
    // explicit imports were found (same-namespace C# files have no `using`).
    if (opts.resolve_edges) {
        store_.buildXRefEdges();
    }

    // VS designer file grouping: detect .cs/.Designer.cs/.resx triples and
    // assign same group_id + insert companion edges.
    if (opts.resolve_edges) {
        store_.groupDesignerTriples();
    }

    // A8: record scan run
    store_.recordScanRun(root, store_.nodeCount(), store_.edgeCount());

    return updated;
}

} // namespace icmg::graph
