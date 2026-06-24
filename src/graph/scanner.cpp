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
#include <set>
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
    {".java","java"}, {".kt","kotlin"}, {".kts","kotlin"},
    {".cs","csharp"}, {".csx","csharp"},
    {".php","php"}, {".php5","php"}, {".phtml","php"},
    {".rb","ruby"}, {".rake","ruby"}, {".gemspec","ruby"},
    {".swift","swift"},
    {".scala","scala"}, {".sc","scala"},
    {".lua","lua"},
    {".dart","dart"},
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

// ── A9: .gitignore / .icmgignore parser ──────────────────────────────────────
//
// Supports the full gitignore feature set:
//   !pattern   → negation (un-ignore a previously ignored path)
//   /prefix    → anchored to repo root
//   suffix/    → match directories only
//   *          → any sequence of chars within one path segment
//   **         → match zero-or-more path segments (recursive wildcard)
//   ?          → any single char within one path segment
//
// Last-matching rule wins (gitignore semantics).

void Scanner::GitIgnore::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        // Strip CR and trailing spaces
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        Pattern p;
        p.negate   = false;
        p.dir_only = false;
        p.anchored = false;

        // '!' prefix → negation
        if (line[0] == '!') {
            p.negate = true;
            line = line.substr(1);
            if (line.empty()) continue;
        }

        // trailing '/' → dir-only
        if (line.back() == '/') {
            p.dir_only = true;
            line.pop_back();
            if (line.empty()) continue;
        }

        // leading '/' → anchored to root
        if (line[0] == '/') {
            p.anchored = true;
            line = line.substr(1);
            if (line.empty()) continue;
        } else if (line.find('/') != std::string::npos) {
            // gitignore: a pattern containing '/' (but not only at the end)
            // is always treated as anchored to the root.
            p.anchored = true;
        }

        p.raw = line;
        patterns.push_back(std::move(p));
    }
}

// globMatch — match a single gitignore pattern against a normalized relative
// path (forward-slash separated, no leading slash).
//
// Rules:
//   ?    matches any single character except '/'
//   *    matches any sequence of characters except '/'
//   **   in segment position matches zero or more path segments
bool Scanner::GitIgnore::globMatch(const std::string& pattern,
                                   const std::string& path) {
    // Split helpers
    auto splitPath = [](const std::string& s, char sep) {
        std::vector<std::string> parts;
        std::string cur;
        for (char c : s) {
            if (c == sep) { if (!cur.empty()) { parts.push_back(cur); cur.clear(); } }
            else cur += c;
        }
        if (!cur.empty()) parts.push_back(cur);
        return parts;
    };

    // Single-segment glob (no '/' in pattern): use character-level matching
    // against each individual path segment (match anywhere in path).
    // Multi-segment pattern: split both and do segment-by-segment matching.

    // Segment-glob: matches a single segment string against a single pattern
    // segment (which must not contain **).
    std::function<bool(const std::string&, size_t,
                       const std::string&, size_t)> segMatch;
    segMatch = [&](const std::string& pat, size_t pi,
                   const std::string& str, size_t si) -> bool {
        while (pi < pat.size() && si < str.size()) {
            char pc = pat[pi];
            if (pc == '*') {
                // Consume all consecutive '*' (single-segment: * never matches /)
                while (pi < pat.size() && pat[pi] == '*') ++pi;
                if (pi == pat.size()) return true; // trailing * matches rest of segment
                // Try matching the remainder of the pattern from every position
                for (size_t k = si; k <= str.size(); ++k)
                    if (segMatch(pat, pi, str, k)) return true;
                return false;
            } else if (pc == '?') {
                // matches exactly one char
                ++pi; ++si;
            } else {
                if (pc != str[si]) return false;
                ++pi; ++si;
            }
        }
        // Skip trailing '*'s
        while (pi < pat.size() && pat[pi] == '*') ++pi;
        return pi == pat.size() && si == str.size();
    };

    // Split pattern into segments on '/'
    std::vector<std::string> patSegs = splitPath(pattern, '/');

    // Normalize path: use '/' uniformly
    std::string normPath = path;
    for (char& c : normPath) if (c == '\\') c = '/';

    std::vector<std::string> pathSegs = splitPath(normPath, '/');

    // Recursive segment-path matcher that handles **
    std::function<bool(size_t, size_t)> matchSegs;
    matchSegs = [&](size_t pi, size_t si) -> bool {
        while (pi < patSegs.size()) {
            if (patSegs[pi] == "**") {
                ++pi; // consume **
                if (pi == patSegs.size()) return true; // ** at end → match anything remaining
                // Try matching the rest of the pattern from every remaining position
                for (size_t k = si; k <= pathSegs.size(); ++k)
                    if (matchSegs(pi, k)) return true;
                return false;
            }
            if (si >= pathSegs.size()) return false;
            if (!segMatch(patSegs[pi], 0, pathSegs[si], 0)) return false;
            ++pi; ++si;
        }
        return si == pathSegs.size();
    };

    bool hasSlash = (pattern.find('/') != std::string::npos);

    if (hasSlash) {
        // Anchored multi-segment: must match path from the root
        return matchSegs(0, 0);
    } else {
        // No slash in pattern: match against any suffix of path segments
        // (i.e., the pattern can appear anywhere in the directory tree).
        // Also try matching just the filename (last segment) for *.ext style.
        // Try matching against each possible starting position in the path
        for (size_t start = 0; start <= pathSegs.size(); ++start) {
            if (matchSegs(0, start)) return true;
        }
        return false;
    }
}

// matches() — gitignore semantics: iterate ALL patterns, apply globMatch,
// track the last match; if that last match is a negation, the path is NOT
// ignored. Returns true if path is ignored.
bool Scanner::GitIgnore::matches(const std::string& relpath) const {
    // Normalize separators once
    std::string norm = relpath;
    for (char& c : norm) if (c == '\\') c = '/';

    bool ignored = false;
    for (auto& pat : patterns) {
        std::string matchPat = pat.raw;
        // For anchored patterns the raw is already stripped of leading '/';
        // globMatch handles the slash-in-pattern anchoring logic.
        // Re-attach leading '/' to force anchored matching inside globMatch.
        if (pat.anchored && matchPat.find('/') == std::string::npos) {
            // If there's no slash in the raw (e.g. "foo" with anchored=true
            // because user wrote "/foo"), synthesize one so globMatch treats
            // it as anchored.
            matchPat = "/" + matchPat;
        }

        if (globMatch(matchPat, norm)) {
            ignored = !pat.negate;
        }
    }
    return ignored;
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
    updated_paths_.clear();   // track which files this scan actually rewrites
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
        // Graph WRITE path. On hosts missing an OpenSSL/CryptoAPI module that
        // SQLCipher's write-side crypto loads at runtime (Windows Server 2019:
        // rsaenh/cryptbase/ntmarta chain -> module err126), upsertNode throws.
        // Degrade: skip this file, keep scanning -- never crash/hang the scan.
        // `icmg context` still serves whatever the graph already holds.
        try {
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
        updated_paths_.push_back(fpath);
        } catch (const std::exception& e) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                std::cerr << "icmg: graph write skipped (" << e.what()
                          << ") -- scan continues without persisting (host crypto/module issue).\n";
            }
            return;
        }
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

    // Strategy 4: class cross-reference — run after scan, even if no explicit
    // imports were found (same-namespace C# files have no `using`). 2026-06-14:
    // incremental xref only re-reads the files this scan changed (huge speedup
    // on `graph update`); full scan re-reads every node.
    if (opts.resolve_edges) {
        if (opts.incremental_xref) {
            std::set<std::string> changed(updated_paths_.begin(), updated_paths_.end());
            store_.buildXRefEdges(&changed);
        } else {
            store_.buildXRefEdges();
        }
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
