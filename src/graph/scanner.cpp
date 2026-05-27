#include "scanner.hpp"
#include "extractor/cpp_extractor.hpp"
#include "symbol_extractor/base_symbol_extractor.hpp"
#include "../core/registry.hpp"
#include "../core/zone_resolver.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
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
    {".sql","sql"},
    // Phase 68: doc/data/config files. Lang label only — no AST extractor;
    // generic extractor still applies (regex symbol scan).
    {".md","markdown"}, {".markdown","markdown"}, {".rst","markdown"},
    {".json","json"}, {".jsonc","json"},
    {".yaml","yaml"}, {".yml","yaml"},
    {".toml","toml"},
    {".xml","xml"},
    {".sh","shell"}, {".bash","shell"}, {".zsh","shell"},
    {".ps1","powershell"},
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
    // T11: load .icmgignore (additive — does not replace .gitignore)
    GitIgnore icmgi;
    icmgi.load(root + "/.icmgignore"); // no-op if absent

    // Phase 21 hotfix: auto-merge case-mismatched duplicate path nodes from
    // pre-v0.6.1 scans (Windows). Cheap when no dups exist (one SELECT).
    int merged = store_.dedupeCaseMixedPaths();
    if (merged > 0) {
        std::cerr << "[icmg] auto-deduped " << merged
                  << " case-mismatched path node(s) from earlier scans\n";
    }

    // Phase 17: zone resolver — auto-tag scanned files by path glob.
    icmg::core::ZoneResolver zoner(store_.db());

    fs::path root_path(root);
    int updated = 0;
    int max_file_size = 2 * 1024 * 1024; // skip files > 2MB

    auto* generic = makeGenericExtractor();

    // Pass 1: upsert all nodes, collect (src_id, src_path, import_name) for Pass 2 resolution
    // Tuple: (src_node_id, src_file_path, import_name_string)
    std::vector<std::tuple<int64_t,std::string,std::string>> pending;

    // Single-file processor (extracted so scan() can fast-path a single file
    // arg without walking siblings — matches user expectation for
    // `icmg graph update <file>`).
    auto processFile = [&](const fs::path& fp, uintmax_t fsz) {
        std::string ext = fp.extension().string();
        std::string lang = detectLang(ext);
        if (!opts.include_langs.empty()) {
            bool found = false;
            for (auto& l : opts.include_langs) if (l == lang) { found = true; break; }
            if (!found) return;
        }
        if (fsz > (uintmax_t)max_file_size) return;
        std::error_code canon_ec;
        auto canon_path = fs::weakly_canonical(fp, canon_ec);
        std::string fpath = canon_ec ? fp.string() : canon_path.string();
#ifdef _WIN32
        if (fpath.size() >= 2 && fpath[1] == ':' &&
            fpath[0] >= 'a' && fpath[0] <= 'z') {
            fpath[0] = (char)(fpath[0] - 'a' + 'A');
        }
#endif
        std::string hash = hashFile(fpath);
        if (opts.skip_stale && !store_.isStale(fpath, hash)) return;
        std::ifstream f(fpath, std::ios::binary);
        if (!f) return;
        std::ostringstream buf; buf << f.rdbuf();
        std::string content = buf.str();
        BaseExtractor* ext_ptr = getExtractor(lang);
        bool own_ext = (ext_ptr != nullptr);
        if (!ext_ptr) ext_ptr = generic;
        ExtractResult result = ext_ptr->extract(fpath, content);
        if (own_ext) delete ext_ptr;
        std::error_code rel_ec;
        auto rel_for_zone = fs::relative(fp, root_path, rel_ec);
        std::string zone_path = rel_ec ? fpath : rel_for_zone.string();
        GraphNode node;
        node.path       = fpath;
        node.lang       = lang;
        node.context    = result.context.substr(0, 500);
        node.symbols    = buildSymbols(result);
        node.size_bytes = (int64_t)fsz;
        node.file_hash  = hash;
        node.zone       = zoner.resolveForPath(zone_path);
        int64_t nodeId = store_.upsertNode(node);
        for (auto& imp : result.imports) pending.emplace_back(nodeId, fpath, imp);
        auto& sym_reg = core::Registry<BaseSymbolExtractor>::instance();
        if (sym_reg.has(lang)) {
            store_.removeSymbolsOf(nodeId);
            auto sym_extractor = sym_reg.create(lang);
            auto symbols = sym_extractor->extractSymbols(fpath, content);
            for (auto& sym : symbols) {
                GraphNode sn;
                sn.path        = fpath + "#" + sym.name;
                sn.lang        = lang;
                sn.parent_id   = nodeId;
                sn.kind        = sym.kind;
                sn.symbol_name = sym.name;
                sn.signature   = sym.signature.substr(0, 240);
                sn.line_start  = sym.line_start;
                sn.line_end    = sym.line_end;
                sn.body_hash   = sym.body_hash;
                sn.zone        = node.zone;
                int64_t symId = store_.upsertNode(sn);
                for (auto& callee : sym.calls) pending.emplace_back(symId, sn.path, "call:" + callee);
                for (auto& base : sym.bases)   pending.emplace_back(symId, sn.path, "ext:" + base);
            }
        }
        ++updated;
    };

    // Single-file fast path: skip directory walk entirely.
    {
        std::error_code rf_ec;
        if (fs::is_regular_file(root_path, rf_ec)) {
            std::error_code fsz_ec;
            auto fsz = fs::file_size(root_path, fsz_ec);
            if (!fsz_ec) processFile(root_path, fsz);
            if (opts.resolve_edges && !pending.empty()) {
                store_.resolveAndInsertEdges(pending);
            }
            return updated;
        }
    }

    // Recursive walk
    std::function<void(const fs::path&, int)> walk = [&](const fs::path& dir, int depth) {
        if (depth > opts.max_depth) return;
        std::error_code iter_ec;
        for (auto& entry : fs::directory_iterator(dir, iter_ec)) {
            if (iter_ec) { iter_ec.clear(); continue; }
            // v1.53.0: skip entries whose path triggers CP_ACP exception
            // (non-1252 chars on Win, e.g. Chinese filenames in plugin caches).
            std::string name;
            try { name = entry.path().filename().string(); }
            catch (const std::exception&) { continue; }

            {
                std::error_code is_dir_ec, is_sym_ec;
                // Skip symlinks and NTFS junctions to prevent infinite recursion.
                if (entry.is_symlink(is_sym_ec)) continue;
                if (entry.is_directory(is_dir_ec)) {
                    // Check ignore_dirs
                    bool skip = false;
                    for (auto& ig : opts.ignore_dirs) {
                        if (name == ig) { skip = true; break; }
                    }
                    // Check gitignore
                    if (!skip && opts.gitignore) {
                        std::error_code rel_ec;
                        std::string rel;
                        try { rel = fs::relative(entry.path(), root_path, rel_ec).string(); }
                        catch (const std::exception&) { rel.clear(); rel_ec = std::make_error_code(std::errc::illegal_byte_sequence); }
                        if (!rel_ec && gi.matches(rel)) skip = true;
                    }
                    // T11: check .icmgignore (always active when file present)
                    if (!skip) {
                        std::error_code rel_ec;
                        std::string rel;
                        try { rel = fs::relative(entry.path(), root_path, rel_ec).string(); }
                        catch (const std::exception&) { rel.clear(); rel_ec = std::make_error_code(std::errc::illegal_byte_sequence); }
                        if (!rel_ec && icmgi.matches(rel)) skip = true;
                    }
                    if (!skip) walk(entry.path(), depth + 1);
                    continue;
                }
            }

            {
                std::error_code is_reg_ec;
                if (!entry.is_regular_file(is_reg_ec)) continue;
            }
            // T11: skip files matched by .icmgignore
            {
                std::error_code rel_ec;
                std::string rel;
                try { rel = fs::relative(entry.path(), root_path, rel_ec).string(); }
                catch (const std::exception&) { rel.clear(); rel_ec = std::make_error_code(std::errc::illegal_byte_sequence); }
                if (!rel_ec && icmgi.matches(rel)) continue;
            }
            std::error_code fsz_ec;
            auto fsz = entry.file_size(fsz_ec);
            if (fsz_ec) continue;
            try { processFile(entry.path(), fsz); }
            catch (const std::exception&) { continue; }
        }
    };

    // v1.20.5: wrap the whole scan + resolve pass in a single SQLite
    // transaction. Without it every per-file `upsertNode()` + per-symbol
    // upsert + per-edge insert was an independent fsync — orders of
    // magnitude slower on large projects (observed: 7+ min on a small repo
    // with thousands of symbols). With a single TX, the same scan completes
    // in seconds. RAII-safe: rollback on exception so partial state never
    // sticks.
    bool _tx_started = false;
    try {
        store_.db().run("BEGIN TRANSACTION");
        _tx_started = true;
    } catch (...) { /* WAL contention; fall through, slow path */ }

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

    if (_tx_started) {
        try { store_.db().run("COMMIT"); }
        catch (...) { try { store_.db().run("ROLLBACK"); } catch (...) {} }
    }

    return updated;
}

} // namespace icmg::graph
