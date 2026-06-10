// Phase 19: context bundle commands.
//   icmg context <file>           — single-call file/symbols/neighbors/memory bundle
//   icmg pack <task>              — task-context aggregator (recall + context + rules)
//   icmg diff-summary             — symbol-aware git-diff summary
//   icmg explain <error>          — match against errors-resolved memory
//   icmg session save/restore     — checkpoint active context

#include "../base_command.hpp"
#include "../cache_emitter.hpp"
#include "../think_directive.hpp"
#include "../auto_zone.hpp"
#include "../model_pricing.hpp"
#include "../../core/intent_slice.hpp"
#include "../../core/read_dedup.hpp"
#include "../../core/registry.hpp"
#include "../../core/tool_call_cache.hpp"
#include <cstdlib>
#include "../../core/config.hpp"
#include "../../core/global_db.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/output_cap.hpp"
#include "../ref_registry.hpp"
#include "../pack_delta.hpp"
#include "../../core/secret_scanner.hpp"
// v1.32.0 B4: pack --rerank via warm-pool LLM.
#include "../../llm/warm_pool.hpp"
#include "../../llm/smart_router.hpp"
#include "../../llm/llama_runner.hpp"
#include <map>
#include <chrono>
#include <ctime>
#include <unordered_set>

namespace icmg::cli {
// Defined in receipt_cmd.cpp — forward decl to avoid extra header.
void writeTokenReceipt(core::Db& db, const std::string& cmd,
                        const std::string& source, const std::string& label,
                        int est_tokens, int raw_tokens = 0);
}
#include "../../graph/graph_store.hpp"
#include "../../graph/scanner.hpp"
#include "../../graph/ast_compressor.hpp"
#include "../../core/token_counter.hpp"
#include "../../compress/compressor.hpp"
#include "../../compress/glossary_store.hpp"
#include "../../imem/memory_store.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <regex>
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <filesystem>

namespace icmg::cli {

static int64_t bndNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::string trunc(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n - 1) + "…";
}

// =============================================================================
// icmg context <file>
// =============================================================================

class ContextCommand : public BaseCommand {
public:
    std::string name()        const override { return "context"; }
    std::string description() const override { return "File context bundle (graph + symbols + memory)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg context <file> [options]\n\n"
            "Options:\n"
            "  --depth N         Neighbor depth (default: 1)\n"
            "  --no-symbols      Skip child symbol list\n"
            "  --no-memory       Skip related memory\n"
            "  --no-content      Skip raw file body excerpt (default: include)\n"
            "  --for INTENT      Emit only lines relevant to INTENT (semantic slice;\n"
            "                    no line-number guessing). Ignored when --lines is set.\n"
            "  --siblings        Also list test/doc/types sibling files (Phase 67)\n"
            "  --symbol NAME     Return only body of named symbol + immediate deps (80%+ token cut)\n"
            "  --lines A-B       Slice content to lines A-B (with line numbers — replaces Read offset/limit)\n"
            "  --max-bytes N     Cap output (default 4096)\n"
            "  --no-cache        Bypass hot-context cache (force recompute)\n"
            "  --full            Re-emit full body even on a cache hit (skip dedup stub)\n"
            "  --json            JSON output\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string file;
        for (auto& a : args) if (!a.empty() && a[0] != '-') { file = a; break; }
        if (file.empty()) { std::cerr << "icmg context: requires <file>\n"; return 1; }

        // v1.28.0 #D fix: tolerate Windows drive-letter paths whose backslash
        // was stripped by shell escape ("D:\\path" -> "D:path" in bash).
        if (file.size() >= 2 && file[1] == ':'
            && ((file[0] >= 'A' && file[0] <= 'Z') || (file[0] >= 'a' && file[0] <= 'z'))
            && (file.size() == 2 || (file[2] != '/' && file[2] != '\\'))) {
            file = file.substr(0, 2) + "/" + file.substr(2);
        }
        for (auto& c : file) if (c == '\\') c = '/';

        // v1.28.0 #B: --exact-path forces absolute resolution, skips fuzzy.
        bool exact_path = hasFlag(args, "--exact-path");
        if (exact_path) {
            std::error_code _ec_ex;
            std::filesystem::path _fp_ex(file);
            if (!_fp_ex.is_absolute()) _fp_ex = std::filesystem::absolute(_fp_ex, _ec_ex);
            _fp_ex = std::filesystem::weakly_canonical(_fp_ex, _ec_ex);
            if (!std::filesystem::exists(_fp_ex, _ec_ex)) {
                std::cerr << "icmg context: --exact-path requires existing file: "
                          << _fp_ex.string() << "\n";
                return 1;
            }
            file = _fp_ex.string();
        } else {
            // v1.29.0 #1: exact-first default. Before falling through to
            // basename fuzzy match inside GraphStore::getNode, try to
            // resolve the input as an absolute existing file. Eliminates
            // the "header.tsx → wrong file silently" failure mode for
            // callers that pass a relative path that happens to exist
            // in cwd.
            std::error_code _ec_ef;
            std::filesystem::path _fp_ef(file);
            if (!_fp_ef.is_absolute()) {
                auto _abs = std::filesystem::absolute(_fp_ef, _ec_ef);
                if (!_ec_ef && std::filesystem::exists(_abs, _ec_ef)) {
                    _fp_ef = std::filesystem::weakly_canonical(_abs, _ec_ef);
                    file = _fp_ef.string();
                }
            }
        }

        bool no_symbols = hasFlag(args, "--no-symbols");
        bool no_memory  = hasFlag(args, "--no-memory");
        size_t cap = 4096;
        try { cap = (size_t)std::stoul(flagValue(args, "--max-bytes", "4096")); } catch (...) {}

        auto& cfg = core::Config::instance();

        // v1.20.2 (bugfix): when caller passes an absolute file path, auto-detect
        // its owning project from the global registry and use THAT project's DB
        // — not the caller's CWD project. This restores the ability to inspect
        // files from another registered project without --project flag.
        // Silent: only switches if file path is absolute AND owned by a different
        // registered project than CWD; falls through to CWD DB otherwise.
        {
            std::error_code _ec_abs;
            std::filesystem::path _fp(file);
            if (_fp.is_absolute() && std::filesystem::exists(_fp, _ec_abs)) {
                auto _norm_fp = std::filesystem::weakly_canonical(_fp, _ec_abs).string();
                std::error_code _ec_cwd;
                auto _cwd = std::filesystem::weakly_canonical(
                    std::filesystem::current_path(_ec_cwd), _ec_cwd).string();
                try {
                    auto& _gdb = core::GlobalDb::instance();
                    _gdb.init();
                    auto _projs = _gdb.listProjects();
                    // Find longest matching project root that contains file path
                    // AND differs from CWD project (avoid pointless switch).
                    std::string _best_db;
                    size_t _best_len = 0;
                    for (auto& _p : _projs) {
                        if (_p.path.empty()) continue;
                        std::error_code _ec_p;
                        auto _proot = std::filesystem::weakly_canonical(_p.path, _ec_p).string();
                        if (_proot.empty()) continue;
                        // Case-insensitive prefix match on Windows; exact on POSIX.
                        auto _norm_lc = _norm_fp;
                        auto _proot_lc = _proot;
#ifdef _WIN32
                        for (auto& c : _norm_lc) c = (char)std::tolower((unsigned char)c);
                        for (auto& c : _proot_lc) c = (char)std::tolower((unsigned char)c);
#endif
                        if (_norm_lc.size() >= _proot_lc.size()
                            && _norm_lc.compare(0, _proot_lc.size(), _proot_lc) == 0
                            && _proot_lc.size() > _best_len) {
                            _best_len = _proot_lc.size();
                            _best_db = _p.db_path;
                        }
                    }
                    // Only override if matched project differs from CWD project.
                    if (!_best_db.empty() && _best_db != cfg.projectDbPath(_cwd)) {
                        cfg.setProjectDbOverride(_best_db);
                    }
                } catch (...) { /* fail-open: stay on CWD DB */ }
            }
        }

        core::Db db(cfg.projectDbPath("."));

        // Phase 74 T5: hot-context cache — re-issue of same `context <file>` w/
        // same opts within TTL returns cached output instantly. Key includes
        // file mtime+size so on-disk edits invalidate naturally.
        bool no_cache = hasFlag(args, "--no-cache") || std::getenv("ICMG_NO_CACHE");
        std::string ctx_cache_args;
        if (!no_cache) {
            namespace fs = std::filesystem;
            std::error_code ec;
            uintmax_t fsz = fs::exists(file, ec) ? fs::file_size(file, ec) : 0;
            std::time_t fmt = 0;
            if (fs::exists(file, ec)) {
                auto ftime = fs::last_write_time(file, ec);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
                fmt = std::chrono::system_clock::to_time_t(sctp);
            }
            ctx_cache_args =
                file + "|mtime=" + std::to_string(fmt)
              + "|sz=" + std::to_string(fsz)
              + "|cap=" + std::to_string(cap)
              + "|nosym=" + (no_symbols ? "1" : "0")
              + "|nomem=" + (no_memory ? "1" : "0")
              + "|nocontent=" + (hasFlag(args, "--no-content") ? "1" : "0")
              + "|sibs=" + (hasFlag(args, "--siblings") ? "1" : "0")
              + "|lines=" + flagValue(args, "--lines")
              + "|for=" + flagValue(args, "--for")
              + "|symbol=" + flagValue(args, "--symbol");
            try {
                core::ToolCallCache tcc(db);
                auto opt = tcc.lookup("context", ctx_cache_args);
                if (opt) {
                    // Read dedup: cache HIT = file unchanged since already shown this
                    // session (key has mtime+size) -> emit a stub, not the full body.
                    bool force_full = hasFlag(args, "--full") || std::getenv("ICMG_NO_DEDUP_STUB");
                    if (core::shouldStubContext(opt->size(), force_full))
                        std::cout << core::contextSeenStub(file, opt->size());
                    else
                        std::cout << *opt;
                    // Boost graph priority: this file is hot for this session.
                    try {
                        db.run("UPDATE graph_nodes SET access_count = access_count + 1, "
                               "updated_at = strftime('%s','now') WHERE path = ?",
                               {file});
                    } catch (...) {}
                    std::cerr << "[icmg context] cache HIT (no recompute)\n";
                    return 0;
                }
            } catch (...) {}
        }
        graph::GraphStore store(db);
        imem::MemoryStore mem(db);

        // v1.29.0 #10: ambiguity warning. If caller passed a bare basename
        // (no '/' or '\\'), enumerate all matching nodes and warn if >1.
        // Reduces silent wrong-file lookups when project has many files
        // sharing a basename (e.g. 19x `header.tsx`).
        if (file.find('/') == std::string::npos && file.find('\\') == std::string::npos
            && !file.empty() && file != "." && file != "..") {
            auto _amb = store.findByBasename(file);
            if (_amb.size() > 1) {
                std::cerr << "[icmg context] WARNING: basename '" << file
                          << "' matches " << _amb.size() << " files. Showing first.\n"
                          << "  Disambiguate with --exact-path or full path. Candidates:\n";
                int _shown = 0;
                for (auto& c : _amb) {
                    std::cerr << "    " << c.path << "\n";
                    if (++_shown >= 5) {
                        if ((int)_amb.size() > 5) std::cerr << "    ... +"
                            << ((int)_amb.size() - 5) << " more\n";
                        break;
                    }
                }
            }
        }
        auto node = store.getNode(file);
        if (!node) {
            // Phase 68: auto-scan-on-miss. Project graph may be stale (file
            // added after last `icmg graph scan`) or extension wasn't
            // covered. Scan this single file inline, then retry getNode.
            // Avoids "not in graph" → Claude falls back to native Read.
            if (std::filesystem::exists(file)) {
                std::cerr << "[icmg context] not in graph; scanning " << file << "...\n";
                graph::Scanner scanner(store);
                graph::Scanner::Options opts;
                scanner.scan(file, opts);
                node = store.getNode(file);
            }
            if (!node) {
                std::cerr << "icmg context: not found in graph: " << file
                          << " (and on-demand scan didn't index it — check ext support / file size)\n";
                return 1;
            }
        } else if (std::filesystem::exists(file)) {
            // v1.28.0 #C: auto-rescan on mtime stale. Previously `icmg
            // context <file>` returned cached `context` column from when
            // the node was indexed — stale if user edited the file since.
            // Compare fs::last_write_time vs node.updated_at; if file is
            // newer, rescan inline so the user sees current content.
            std::error_code _mt_ec;
            auto _mt = std::filesystem::last_write_time(file, _mt_ec);
            if (!_mt_ec) {
                // v1.53.0 fix: file_clock epoch (Win=1601, sometimes 2174) != Unix.
                // Convert to system_clock (Unix epoch) before compare with
                // node->updated_at (Unix seconds). Without this, every call
                // triggered spurious rescan because file_mt was always ~3.6e8 ahead.
                int64_t file_mt;
                #if defined(_MSC_VER) && _MSC_VER >= 1920
                  auto _sys = std::chrono::clock_cast<std::chrono::system_clock>(_mt);
                  file_mt = std::chrono::duration_cast<std::chrono::seconds>(
                      _sys.time_since_epoch()).count();
                #else
                  // Cross-platform fallback: subtract Win FILETIME-Unix epoch
                  // delta (369 years = 11644473600 s). Safe on libstdc++ where
                  // file_clock == system_clock (delta=0).
                  int64_t raw = std::chrono::duration_cast<std::chrono::seconds>(
                      _mt.time_since_epoch()).count();
                  file_mt = (raw > 11644473600LL) ? raw - 11644473600LL : raw;
                #endif
                if (file_mt > node->updated_at + 5) {
                    std::cerr << "[icmg context] stale (file mtime "
                              << (file_mt - node->updated_at)
                              << "s > node.updated_at) — rescanning...\n";
                    graph::Scanner scanner(store);
                    graph::Scanner::Options opts;
                    scanner.scan(file, opts);
                    auto fresh = store.getNode(file);
                    if (fresh) node = fresh;
                }
            }
        }

        // Phase 82 T1: --symbol NAME — return only the named symbol body + deps.
        // 80%+ token cut: file 500 lines → 20-40 relevant lines.
        std::string sym_filter = flagValue(args, "--symbol");
        if (!sym_filter.empty()) {
            auto kids = store.childrenOf(node->id);
            graph::GraphNode* sym_node = nullptr;
            // Case-insensitive substring match.
            std::string sym_lower = sym_filter;
            for (auto& c : sym_lower) c = (char)std::tolower((unsigned char)c);
            for (auto& k : kids) {
                std::string kl = k.symbol_name;
                for (auto& c : kl) c = (char)std::tolower((unsigned char)c);
                if (kl == sym_lower || kl.find(sym_lower) != std::string::npos) {
                    sym_node = &k; break;
                }
            }
            if (!sym_node) {
                std::cerr << "icmg context --symbol: '" << sym_filter
                          << "' not found in " << file << "\n";
                std::cerr << "Available: ";
                for (size_t i = 0; i < kids.size() && i < 10; ++i) {
                    if (i) std::cerr << ", ";
                    std::cerr << kids[i].symbol_name;
                }
                std::cerr << "\n";
                return 1;
            }
            std::ostringstream sym_out;
            sym_out << "Symbol: [" << sym_node->kind << "] " << sym_node->symbol_name
                    << "  L" << sym_node->line_start << "-" << sym_node->line_end
                    << "  " << node->path << "\n";
            if (!sym_node->context.empty())
                sym_out << "Context: " << trunc(sym_node->context, 120) << "\n";
            // Body: read file slice.
            std::ifstream sf(node->path, std::ios::binary);
            if (sf) {
                int cur = 0;
                std::string ln;
                sym_out << "\n--- Body ---\n";
                while (std::getline(sf, ln)) {
                    ++cur;
                    if (cur < sym_node->line_start) continue;
                    if (cur > sym_node->line_end)   break;
                    sym_out << cur << "\t" << ln << "\n";
                }
            }
            std::string result = sym_out.str();
            if (result.size() > cap) result = result.substr(0, cap - 3) + "...\n";
            std::cout << result;
            if (!no_cache && !ctx_cache_args.empty()) {
                try { core::ToolCallCache tcc(db); tcc.store("context", ctx_cache_args, result); } catch (...) {}
            }
            return 0;
        }

        std::ostringstream out;
        out << "File: " << node->path << "  (lang=" << node->lang
            << ", " << node->size_bytes << " B, zone=" << node->zone << ")\n";
        if (!node->context.empty())
            out << "Context: " << trunc(node->context, 200) << "\n";

        // Imports + Used-by (1-hop)
        auto outgoing = store.edgesFrom(node->id);
        auto incoming = store.edgesTo(node->id);
        if (!outgoing.empty()) {
            out << "Imports: ";
            int n = 0;
            for (auto& e : outgoing) {
                if (n++ >= 8) { out << "..."; break; }
                std::string p;
                db.query("SELECT path FROM graph_nodes WHERE id=?",
                         {std::to_string(e.dst)},
                         [&](const core::Row& r){ if(!r.empty()) p=r[0]; });
                if (!p.empty()) {
                    namespace fs = std::filesystem;
                    out << fs::path(p).filename().string();
                    out << "(" << e.edge_type << ") ";
                }
            }
            out << "\n";
        }
        if (!incoming.empty()) {
            out << "Used by: ";
            int n = 0;
            for (auto& e : incoming) {
                if (n++ >= 8) { out << "..."; break; }
                std::string p;
                db.query("SELECT path FROM graph_nodes WHERE id=?",
                         {std::to_string(e.src)},
                         [&](const core::Row& r){ if(!r.empty()) p=r[0]; });
                if (!p.empty()) {
                    namespace fs = std::filesystem;
                    out << fs::path(p).filename().string() << " ";
                }
            }
            out << "\n";
        }

        // Child symbols
        if (!no_symbols) {
            auto kids = store.childrenOf(node->id);
            if (!kids.empty()) {
                out << "Symbols (" << kids.size() << "):\n";
                for (auto& s : kids) {
                    out << "  [" << s.kind << "] " << s.symbol_name
                        << "  L" << s.line_start << "-" << s.line_end << "\n";
                }
            }
        }

        // Related memory (best-effort recall by file basename)
        if (!no_memory) {
            namespace fs = std::filesystem;
            std::string base = fs::path(node->path).stem().string();
            if (!base.empty()) {
                auto results = mem.recall(base, 3, false);
                if (!results.empty()) {
                    out << "Memory (top 3 for \"" << base << "\"):\n";
                    for (auto& m : results) {
                        out << "  [" << std::fixed << std::setprecision(1) << m.score
                            << "] " << trunc(m.topic, 60) << "\n";
                    }
                }
            }
        }

        // Phase 67 T19: --siblings — auto-include test / doc / types siblings.
        // Saves the typical "what does X test?" / "where are X types?" follow-up
        // queries by emitting paths upfront. Off by default to keep --max-bytes
        // budget tight; users opt in via flag.
        if (hasFlag(args, "--siblings")) {
            namespace fs = std::filesystem;
            fs::path src(node->path);
            std::string stem = src.stem().string();
            std::string ext  = src.extension().string();
            fs::path dir     = src.parent_path();
            std::vector<std::string> sibs = {
                (dir / (stem + ".test" + ext)).string(),
                (dir / (stem + ".spec" + ext)).string(),
                (dir / (stem + ".d.ts")).string(),
                (dir / (stem + ".types" + ext)).string(),
                (dir / ("test_" + stem + ext)).string(),
                (dir / (stem + "_test" + ext)).string(),
                (dir / (stem + ".md")).string(),
                ("tests/test_" + stem + ext),
                ("docs/" + stem + ".md"),
            };
            std::vector<std::string> found;
            for (auto& s : sibs) if (fs::exists(s)) found.push_back(s);
            if (!found.empty()) {
                out << "Siblings: ";
                for (size_t i = 0; i < found.size(); ++i) {
                    if (i) out << ", ";
                    out << found[i];
                }
                out << "\n";
            }
        }

        // Phase 67 hotfix: emit actual file CONTENT excerpt so Claude doesn't
        // fall back to raw Read after `icmg context`. Default ON; opt-out
        // with --no-content. Body uses ~70% of remaining cap budget.
        // Phase 67 T26: --lines START-END slice — mirrors Read tool offset/limit
        // so Claude doesn't fall back to native Read for line ranges.
        bool no_content = hasFlag(args, "--no-content");
        std::string lines_arg = flagValue(args, "--lines");
        std::string for_intent = flagValue(args, "--for");  // semantic single-file slice
        int line_start = 0, line_end = 0;
        if (!lines_arg.empty()) {
            auto dash = lines_arg.find('-');
            try {
                if (dash != std::string::npos) {
                    line_start = std::stoi(lines_arg.substr(0, dash));
                    line_end   = std::stoi(lines_arg.substr(dash + 1));
                } else {
                    line_start = line_end = std::stoi(lines_arg);
                }
            } catch (...) { line_start = line_end = 0; }
            if (line_start <= 0) line_start = 1;
            if (line_end < line_start) line_end = line_start;
        }
        if (!no_content) {
            std::string meta = out.str();
            size_t budget = (meta.size() < cap) ? (cap - meta.size()) : 256;
            // Reserve some headroom for header + newlines.
            if (budget > 256) budget -= 64;
            namespace fs = std::filesystem;
            // Try graph-stored path first; fall back to user-supplied path.
            std::vector<std::string> candidates = {node->path, file};
            std::string body;
            std::string resolved;
            for (auto& cand : candidates) {
                std::ifstream f(cand, std::ios::binary);
                if (!f) continue;
                std::ostringstream ss; ss << f.rdbuf();
                body = ss.str();
                resolved = cand;
                break;
            }
            if (!body.empty()) {
                if (line_start > 0) {
                    // Slice to requested line range. Output preserves line
                    // numbers via `cat -n` style header so Claude can locate
                    // edit anchors without re-running Read.
                    std::istringstream is(body);
                    std::ostringstream sliced;
                    std::string line;
                    int n = 0;
                    while (std::getline(is, line)) {
                        ++n;
                        if (n < line_start) continue;
                        if (n > line_end) break;
                        sliced << std::setw(5) << n << "  " << line << "\n";
                    }
                    std::string slice_body = sliced.str();
                    bool truncated = slice_body.size() > budget;
                    if (truncated) slice_body.resize(budget);
                    out << "\n--- Content (" << resolved
                        << " lines " << line_start << "-" << line_end
                        << (truncated ? "; truncated" : "")
                        << ") ---\n" << slice_body;
                    if (truncated) out << "\n--- [slice truncated; raise --max-bytes] ---\n";
                } else if (!for_intent.empty()) {
                    // --for: emit only the lines relevant to the intent (no range guessing).
                    auto ranges = core::intentSliceRanges(body, for_intent, /*ctx*/6, /*maxRanges*/4, /*maxTotal*/120);
                    std::vector<std::string> all; { std::istringstream is(body); std::string ln; while (std::getline(is, ln)) all.push_back(ln); }
                    std::ostringstream rend;
                    for (auto& r : ranges) {
                        rend << "--- lines " << r.start << "-" << r.end << " ---\n";
                        for (int n = r.start; n <= r.end && n <= (int)all.size(); ++n)
                            rend << std::setw(5) << n << "  " << all[(size_t)n - 1] << "\n";
                    }
                    std::string slice_body = rend.str();
                    if (slice_body.empty()) {
                        out << "\n--- Content (" << resolved << "; no line matched \"" << for_intent
                            << "\" -- try `icmg context " << resolved << "` for the full file) ---\n";
                    } else {
                        bool truncated = slice_body.size() > budget;
                        if (truncated) slice_body.resize(budget);
                        out << "\n--- Content (" << resolved << " for \"" << for_intent << "\"; "
                            << ranges.size() << " window(s)" << (truncated ? "; truncated" : "") << ") ---\n" << slice_body;
                        if (truncated) out << "\n--- [slice truncated; raise --max-bytes] ---\n";
                    }
                } else {
                    bool truncated = body.size() > budget;
                    if (truncated) body.resize(budget);
                    out << "\n--- Content (" << resolved
                        << (truncated ? "; truncated" : "")
                        << ") ---\n" << body;
                    if (truncated) out << "\n--- [content truncated; raise --max-bytes for more] ---\n";
                }
            } else {
                out << "\n--- Content unavailable (graph path mismatch; try `icmg context "
                    << node->path << "`) ---\n";
            }
        }

        // Cap output
        std::string spill;
        std::string capped = core::capOutput(out.str(), cap, spill);
        std::cout << capped;

        // Phase 74 T5: store + boost. Cache 30min default. Hot file = high
        // priority on next graph search.
        if (!no_cache && !ctx_cache_args.empty()) {
            try {
                core::ToolCallCache tcc(db);
                tcc.store("context", ctx_cache_args, capped, /*ttl_sec*/ 1800);
                db.run("UPDATE graph_nodes SET access_count = access_count + 1, "
                       "updated_at = strftime('%s','now') WHERE path = ?",
                       {file});
            } catch (...) {}
        }
        return 0;
    }
};

// =============================================================================
// icmg pack <task>
// =============================================================================

class PackCommand : public BaseCommand {
public:
    std::string name()        const override { return "pack"; }
    std::string description() const override { return "Task-context bundle (recall + files + rules)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg pack <task description...> [options]\n\n"
            "Options:\n"
            "  --zone Z              Scope to zone\n"
            "  --max-bytes N         Cap output (default 4096)\n"
            "  --memory-limit N      Recall result count (default 5)\n"
            "  --cache-prefix        Wrap output in prompt-cache markers\n"
            "  --auto-cache          Auto-wrap when output >= 4KB (Phase 67)\n"
            "  --no-compress         Skip auto-compress (Phase 71 — default ON >=1KB)\n"
            "  --raw                 Alias for --no-compress (v1.28.0)\n"
            "  --compress-aggressive Stronger lossy compress (filler-strip)\n"
            "  --cache-ttl N         Cache TTL seconds (default 3600)\n"
            "  --no-think            Force directive: skip model analysis pass\n"
            "  --concise             Stronger directive: short reply, no code\n"
            "  --sayless             Strongest: ultra-terse fragment-style reply (~60 words)\n"
            "  --auto-think          Classify task; apply --no-think if simple (DEFAULT)\n"
            "  --full-think          Opt out of auto-think — keep full thinking pass\n"
            "  --thinking-stats      Show 30-day thinking-budget telemetry\n"
            "  --diff                Emit only delta vs previous pack (60-90% smaller on repeats)\n"
            "  --diff-reset          Clear stored last-pack baseline\n"
            "  --no-diff             Disable auto-diff even when previous pack exists\n"
            "  --preset NAME         Task-type preset: fix-bug|add-command|release\n"
            "  --ref-ids             Emit [ICMG-MEM-N] anchors; subsequent calls reuse\n"
            "  --prune-audit         Drop memory hits below --prune-min-score (default 1.5)\n"
            "  --rerank              LLM-rerank memory hits by relevance to task (v1.32.0)\n"
            "  --prune-min-score N   Threshold for prune-audit (default 1.5)\n"
            "  --budget N            Knapsack-keep highest-score hits within N tokens\n"
            "  --compress-ast        Elide function/class bodies in emitted source (70-90% byte cut)\n"
            "  --show-tokens         Print before->after token stats to stderr\n  --allow-secrets       Skip secret scan entirely (emit verbatim)\n  --strict-secrets      Scan + redact, exit 2 if secrets detected\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }

        // Phase 41 T4: --thinking-stats subaction.
        if (hasFlag(args, "--thinking-stats")) {
            auto& cfg = core::Config::instance();
            core::Db db(cfg.projectDbPath("."));
            int total=0, simple=0, complex_=0, unk=0, nt=0, conc=0;
            int64_t bytes_total=0;
            try {
                db.query("SELECT COUNT(*), "
                         " SUM(CASE WHEN intent='simple' THEN 1 ELSE 0 END), "
                         " SUM(CASE WHEN intent='complex' THEN 1 ELSE 0 END), "
                         " SUM(CASE WHEN intent='unknown' THEN 1 ELSE 0 END), "
                         " SUM(no_think), SUM(concise), COALESCE(SUM(input_bytes),0) "
                         "FROM thinking_telemetry "
                         "WHERE created_at > strftime('%s','now') - 30*86400",
                         {}, [&](const core::Row& r){
                             if (r.size() < 7) return;
                             total = std::stoi(r[0]);
                             simple = std::stoi(r[1]);
                             complex_ = std::stoi(r[2]);
                             unk = std::stoi(r[3]);
                             nt = std::stoi(r[4]);
                             conc = std::stoi(r[5]);
                             bytes_total = std::stoll(r[6]);
                         });
            } catch (...) {}
            int est_thinking_saved = nt * 1500;  // est 1.5K thinking tok saved per no-think call
            // Honest per-model output rate (thinking tok = output); ICMG_MODEL or default Sonnet.
            const char* pm = std::getenv("ICMG_MODEL");
            double out_rate = modelPricing(pm ? pm : "").out;
            std::cout << "Thinking-budget telemetry (last 30d):\n"
                      << "  total calls:     " << total << "\n"
                      << "    simple:        " << simple << "\n"
                      << "    complex:       " << complex_ << "\n"
                      << "    unknown:       " << unk << "\n"
                      << "  no-think applied: " << nt << "\n"
                      << "  concise mode:    " << conc << "\n"
                      << "  total bytes out: " << bytes_total << "\n"
                      << "  est thinking tok saved: " << est_thinking_saved
                      << " (~$" << (est_thinking_saved * out_rate / 1000000.0) << " at $" << out_rate << "/MTok output)\n";
            return 0;
        }

        std::string task;
        for (auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            if (!task.empty()) task += " ";
            task += a;
        }
        // --preset: map task-type shortcut to task string when none provided.
        std::string preset = flagValue(args, "--preset");
        if (!preset.empty() && task.empty()) {
            if      (preset == "fix-bug")      task = "fix bug investigate error debug";
            else if (preset == "add-command")  task = "add new command feature implementation";
            else if (preset == "release")      task = "release version changelog public artifact";
            else                               task = preset;
        }
        if (task.empty()) { std::cerr << "icmg pack: requires <task> or --preset\n"; return 1; }

        std::string zone = flagValue(args, "--zone");
        // Phase 72: auto-zone detect from task keywords when user didn't pass
        // --zone. Heuristic only; sharpens BM25 IDF without manual flag.
        // Bypass via --no-auto-zone.
        if (zone.empty() && !hasFlag(args, "--no-auto-zone")) {
            std::string inferred = cli::inferZone(task);
            if (!inferred.empty()) {
                zone = inferred;
                std::cerr << "[icmg pack] auto-zone: " << zone
                          << " (use --no-auto-zone to skip)\n";
            }
        }
        size_t cap = 4096;
        try { cap = (size_t)std::stoul(flagValue(args, "--max-bytes", "4096")); } catch (...) {}
        int mem_limit = 5;
        try { mem_limit = std::stoi(flagValue(args, "--memory-limit", "5")); } catch (...) {}

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        // Phase 45 T1: tool-call cache. Key = (cmd, normalized args).
        bool no_cache = hasFlag(args, "--no-cache") || std::getenv("ICMG_NO_CACHE");
        std::string cache_args = task + "|zone=" + zone
                               + "|cap=" + std::to_string(cap)
                               + "|mem=" + std::to_string(mem_limit);
        core::ToolCallCache tcc(db);
        std::string capped;  // pre-directive raw output
        std::optional<std::string> cached;
        if (!no_cache) cached = tcc.lookup("pack", cache_args);
        if (cached) {
            capped = *cached;
            std::cerr << "[icmg pack] cache HIT (skip recompute)\n";
        } else {

        imem::MemoryStore mem(db);
        graph::GraphStore store(db);

        // Phase 67 T2: per-session ref registry (opt-in via --ref-ids).
        bool use_refs = hasFlag(args, "--ref-ids");
        RefRegistry refs(std::filesystem::current_path().string());
        // Phase 67 T6: --prune-audit drops low-score memory hits (score < 1.5)
        // and empty-context graph hits before final output. Heuristic
        // self-prune; no LLM round-trip.
        bool prune_audit = hasFlag(args, "--prune-audit");
        double prune_threshold = 1.5;
        try { prune_threshold = std::stod(flagValue(args, "--prune-min-score", "1.5")); } catch (...) {}
        // Phase 67 T16: --budget N — knapsack-rank memory hits by score/cost,
        // drop lowest until estimated tokens under N. cap is bytes; budget is tokens.
        int token_budget = 0;
        try { token_budget = std::stoi(flagValue(args, "--budget", "0")); } catch (...) {}

        std::ostringstream out;
        out << "# Task Context: " << trunc(task, 80) << "\n\n";

        // 1. Memory recall
        auto recalled = zone.empty() ? mem.recall(task, mem_limit, false)
                                       : mem.recallInZone(task, zone, mem_limit, false);
        // Phase 67 T6: prune low-score memory hits when --prune-audit set.
        if (prune_audit) {
            size_t before = recalled.size();
            recalled.erase(
                std::remove_if(recalled.begin(), recalled.end(),
                    [prune_threshold](const auto& m){ return m.score < prune_threshold; }),
                recalled.end());
            size_t pruned = before - recalled.size();
            if (pruned > 0) {
                std::cerr << "[icmg pack] --prune-audit dropped "
                          << pruned << " memory hit(s) below score "
                          << prune_threshold << "\n";
            }
        }
        // Phase 67 T16: knapsack — sort by score desc, accumulate until budget hit.
        if (token_budget > 0 && !recalled.empty()) {
            std::sort(recalled.begin(), recalled.end(),
                      [](const auto& a, const auto& b){ return a.score > b.score; });
            size_t accum_tok = 0;
            size_t kept = 0;
            for (auto& m : recalled) {
                size_t est = (m.topic.size() + m.content.size()) / 4 + 4;
                if (accum_tok + est > (size_t)token_budget) break;
                accum_tok += est;
                ++kept;
            }
            if (kept < recalled.size()) {
                std::cerr << "[icmg pack] --budget " << token_budget << " tok kept "
                          << kept << "/" << recalled.size() << " hit(s) ("
                          << accum_tok << " tok est)\n";
                recalled.resize(kept);
            }
        }
        // v1.32.0 B4: --rerank — ask local LLM to score each remaining hit
        // for relevance to the task, reorder by LLM score. Router-gated;
        // silently no-ops if router routes REGEX (e.g., build off, opt-out,
        // cold-load fail, p95 spike). Latency budget: warm path (<2s SLA).
        if (hasFlag(args, "--rerank") && !recalled.empty()) {
            llm::CallContext rctx;
            rctx.tier             = llm::PathTier::WARM;
            rctx.kind             = "rerank";
            rctx.input_tokens_est = recalled.size() * 30 + task.size() / 4;
            rctx.build_has_llama  = llm::LlamaRunner::available();
            rctx.llm_loaded       = llm::WarmPool::instance().isLoaded();
            const char* dis = std::getenv("ICMG_LLM_USER_DISABLED");
            rctx.user_disabled = (dis && *dis == '1');
            // --rerank is an explicit user opt-in for local LLM: bypass the
            // no-premium gate (2026-06-06) so it still works in Claude sessions.
            rctx.explicit_local = true;
            auto rd = llm::routeFor(rctx);
            if (rd.route == llm::Route::LLM_LOCAL) {
                std::string err;
                llm::LlamaRunner* run = llm::WarmPool::instance().acquire(err);
                if (run) {
                    std::ostringstream pp;
                    pp << "You are a relevance ranker for AI coding context.\n"
                       << "Task: " << task << "\n\nHits:\n";
                    for (size_t i = 0; i < recalled.size(); ++i) {
                        pp << (i + 1) << ". " << recalled[i].topic << " | "
                           << recalled[i].content.substr(0, 80) << "\n";
                    }
                    pp << "\nReply with one line: \"RANK: i1,i2,i3,...\" "
                          "ordering hit numbers most-relevant first. No prose.\n";
                    llm::InferParams ip;
                    ip.max_tokens = 96;
                    ip.temperature = 0.0f;
                    ip.stop = "\n\n";
                    auto res = run->infer(pp.str(), ip);
                    if (res.ok) {
                        auto pos = res.text.find("RANK:");
                        if (pos != std::string::npos) {
                            std::string list = res.text.substr(pos + 5);
                            std::vector<size_t> order;
                            std::string num;
                            auto flush = [&]() {
                                if (num.empty()) return;
                                try {
                                    size_t idx = std::stoul(num) - 1;
                                    if (idx < recalled.size()) order.push_back(idx);
                                } catch (...) {}
                                num.clear();
                            };
                            for (char c : list) {
                                if (std::isdigit(static_cast<unsigned char>(c))) num.push_back(c);
                                else flush();
                            }
                            flush();
                            if (!order.empty()) {
                                std::vector<bool> seen(recalled.size(), false);
                                decltype(recalled) reordered;
                                reordered.reserve(recalled.size());
                                for (size_t idx : order) {
                                    if (!seen[idx]) { reordered.push_back(recalled[idx]); seen[idx] = true; }
                                }
                                for (size_t i = 0; i < recalled.size(); ++i)
                                    if (!seen[i]) reordered.push_back(recalled[i]);
                                recalled = std::move(reordered);
                                std::cerr << "[icmg pack] --rerank applied via LLM "
                                          << "(wall=" << static_cast<int>(res.wall_ms) << "ms)\n";
                            }
                        }
                    }
                } else {
                    std::cerr << "[icmg pack] --rerank skipped: " << err << "\n";
                }
            } else {
                std::cerr << "[icmg pack] --rerank routed " << rd.reason << " — skip\n";
            }
        }

        if (!recalled.empty()) {
            size_t mem_start = out.tellp();
            out << "## Memory (top " << recalled.size() << ")\n";
            for (auto& m : recalled) {
                std::string body = trunc(m.content, 120);
                if (use_refs) {
                    bool prior = refs.seen("MEM", body);
                    std::string ref = refs.getOrAssign("MEM", body);
                    if (prior && !ref.empty()) {
                        // Subsequent emission — reference only, body deduped.
                        out << "- Reuse " << ref << " ["
                            << std::fixed << std::setprecision(1) << m.score
                            << "] " << trunc(m.topic, 70) << "\n";
                    } else {
                        // First emission — full body + ID anchor.
                        out << "- " << (ref.empty() ? "" : ref + " ")
                            << "[" << std::fixed << std::setprecision(1) << m.score
                            << "] " << trunc(m.topic, 70) << "\n  " << body << "\n";
                    }
                } else {
                    out << "- [" << std::fixed << std::setprecision(1) << m.score
                        << "] " << trunc(m.topic, 70) << "\n  " << body << "\n";
                }
            }
            out << "\n";
            // Phase 67 T1: receipt — bytes/4 ≈ tokens
            size_t mem_bytes = (size_t)out.tellp() - mem_start;
            // Raw baseline: full (un-truncated) content of emitted nodes.
            int mem_raw_tok = 0;
            for (auto& rm : recalled)
                mem_raw_tok += (int)((rm.topic.size() + rm.content.size()) / 4) + 4;
            writeTokenReceipt(db, "pack", "memory",
                              "top " + std::to_string(recalled.size()),
                              (int)(mem_bytes / 4), mem_raw_tok);
        }

        // 2. Mentioned files: scan task tokens, look up symbol or file
        std::regex word_re(R"(\b[A-Za-z_][A-Za-z0-9_]{2,})");
        std::ostringstream files_section;
        std::set<int64_t> seen_ids;
        int file_hits = 0;
        for (auto it = std::sregex_iterator(task.begin(), task.end(), word_re);
             it != std::sregex_iterator() && file_hits < 5; ++it) {
            std::string tok = (*it)[0].str();
            // Skip very common short tokens
            static const std::set<std::string> stop = {
                "the","and","for","fix","bug","when","why","how",
                "what","this","that","must","can","run","get"};
            std::string tlo = tok; std::transform(tlo.begin(), tlo.end(), tlo.begin(), ::tolower);
            if (stop.count(tlo)) continue;

            // Try symbol first
            auto syms = store.findSymbol(tok);
            for (auto& s : syms) {
                if (seen_ids.insert(s.id).second) {
                    // Phase 82 T3: cross-call session dedup — skip body if
                    // this file was already emitted in a prior pack this session.
                    bool already_seen = refs.seen("FILE", s.path);
                    if (!already_seen) refs.getOrAssign("FILE", s.path);
                    if (already_seen) {
                        files_section << "### " << s.symbol_name
                                      << " [already in context — " << s.path << "]\n";
                    } else {
                        files_section << "### " << s.symbol_name
                                      << " (" << s.kind << ", L" << s.line_start
                                      << "-" << s.line_end << ")\n"
                                      << "Path: " << s.path << "\n\n";
                    }
                    ++file_hits;
                }
            }
        }
        if (file_hits > 0) {
            size_t fs_start = out.tellp();
            out << "## Files & Symbols (" << file_hits << ")\n" << files_section.str();
            size_t fs_bytes = (size_t)out.tellp() - fs_start;
            // Raw baseline: avg full file ~800 tokens vs symbol-slice emitted.
            writeTokenReceipt(db, "pack", "graph",
                              std::to_string(file_hits) + " hit(s)",
                              (int)(fs_bytes / 4), file_hits * 800);
        }

        // Phase 28 T2: detect "like X" / "copy of X" / "modeled on X" -> auto-include
        // matching template manifest summary.
        {
            std::regex re_like(R"(\b(?:like|similar to|modeled on|based on|copy of)\s+(\w+))",
                               std::regex::ECMAScript | std::regex::icase);
            std::smatch m;
            if (std::regex_search(task, m, re_like) && m.size() >= 2) {
                std::string ref_name = m[1].str();
                // Lookup template by source path stem matching ref_name.
                std::string mj;
                db.query("SELECT manifest_json FROM templates "
                         "WHERE name = ? OR source_path LIKE ? OR source_path LIKE ? LIMIT 1",
                         {ref_name, "%/" + ref_name + ".%", "%\\" + ref_name + ".%"},
                         [&](const core::Row& r){ if (!r.empty()) mj = r[0]; });
                if (!mj.empty()) {
                    out << "## Reference template: " << ref_name << "\n";
                    try {
                        auto j = nlohmann::json::parse(mj);
                        out << "  source: " << j.value("source", "") << "\n";
                        if (j.contains("required_symbols") && j["required_symbols"].is_array()) {
                            out << "  required_symbols (" << j["required_symbols"].size() << "):\n";
                            for (auto& s : j["required_symbols"]) {
                                out << "    - " << s.value("name", "")
                                    << " (" << s.value("kind", "") << ")\n";
                            }
                        }
                        if (j.contains("structural_markers") && j["structural_markers"].is_array()) {
                            out << "  structural_markers:\n";
                            for (size_t i = 0; i < j["structural_markers"].size(); ++i) {
                                std::string sm = j["structural_markers"][i];
                                out << "    " << sm << "\n";
                            }
                        }
                    } catch (...) {
                        out << "  (manifest parse failed)\n";
                    }
                    out << "\n  Verify after creation: `icmg parity " << ref_name
                        << " <new-file>` or `icmg template apply " << ref_name
                        << " --to <new-file> --check`\n\n";
                }
            }
        }

        std::string spill;
        capped = core::capOutput(out.str(), cap, spill);
        if (!no_cache) tcc.store("pack", cache_args, capped, 300);
        }  // end else (cache-miss compute branch)

        // T10: --compress-ast — elide function/class bodies in the packed output.
        // Applied after cache-miss compute (so we compress the final assembled text).
        // --show-tokens emits before→after token stats to stderr.
        bool compress_ast  = hasFlag(args, "--compress-ast");
        bool show_tokens   = hasFlag(args, "--show-tokens");
        if (compress_ast && !capped.empty()) {
            try {
                size_t tok_before = core::estimateTokens(capped);
                // The pack output is markdown text, not a single-lang source file.
                // We apply C++ body elision to any fenced code blocks we detect.
                // For the common case (pack returns symbol paths + metadata, not
                // raw source), we also apply cpp compression to the whole blob
                // so any inline code snippets are trimmed.
                // Language: use "cpp" as default for C-family heuristic on mixed output.
                std::string compressed = graph::compressAst(capped, "cpp");
                if (show_tokens) {
                    size_t tok_after = core::estimateTokens(compressed);
                    int pct = (tok_before > 0)
                              ? (int)(100 - (100 * tok_after / tok_before))
                              : 0;
                    std::cerr << "pack: " << tok_before << " → " << tok_after
                              << " tokens (-" << pct << "%)\n";
                }
                if (compressed.size() < capped.size()) capped = compressed;
            } catch (...) {}
        } else if (show_tokens) {
            // --show-tokens without --compress-ast: just report current size.
            size_t tok = core::estimateTokens(capped);
            std::cerr << "pack: " << tok << " tokens (--compress-ast not set, no reduction)\n";
        }

        // Phase 40 T1: optional Anthropic prompt-cache wrap.
        // Phase 67 T12: --auto-cache enables cache-prefix when output >= 4KB
        // (Anthropic recommends caching when prefix > ~1K tokens for ROI).
        bool want_cache = hasFlag(args, "--cache-prefix");
        if (hasFlag(args, "--auto-cache") && capped.size() >= 4096) {
            want_cache = true;
            std::cerr << "[icmg pack] auto-cache: output " << capped.size()
                      << "B >= 4096B threshold, wrapping in cache markers\n";
        }
        if (want_cache) {
            int ttl = 3600;
            try {
                std::string t = flagValue(args, "--cache-ttl");
                if (!t.empty()) ttl = std::stoi(t);
            } catch (...) {}
            cli::CacheEmitOptions o; o.ttl_seconds = ttl;
            capped = cli::wrapCachePrefix(capped, o);
        }

        // Phase 70: auto-compress when output ≥ threshold. Default ON; opt-out
        // via --no-compress. Lossless mode; emits compressed body + glossary
        // header so model can interpret aliases. Threshold 6KB (1.5K tok)
        // lower than compressor's internal 8K to trigger more often on pack.
        // v1.28.0 #A: `--raw` alias for `--no-compress` (uniform semantic
        // across icmg cmds: run/ingest/fetch/context all opt out via `--raw`).
        bool no_compress = hasFlag(args, "--no-compress") || hasFlag(args, "--raw");
        size_t comp_threshold = 1024;
        try { comp_threshold = (size_t)std::stoul(flagValue(args, "--compress-min", "1024")); } catch (...) {}
        if (!no_compress && capped.size() >= comp_threshold) {
            try {
                compress::CompressOptions copts;
                copts.mode = hasFlag(args, "--compress-aggressive")
                              ? compress::Mode::Aggressive
                              : compress::Mode::Lossless;
                // Phase 73: match pack auto-fire threshold. comp_threshold is bytes,
                // threshold_tok is tokens (~bytes/4). 1024B → ~256 tok.
                copts.threshold_tok = (int)(comp_threshold / 4);
                compress::Compressor c(copts);
                auto cr = c.compress(capped, "pack");
                if (!cr.skipped && cr.tok_out > 0 && cr.tok_out < cr.tok_in) {
                    // Persist telemetry so dashboard reflects this auto-call.
                    try {
                        compress::GlossaryStore store(db);
                        store.save(cr.content_hash, cr.glossary);
                        store.recordTelemetry("compress-auto", cr.bytes_in, cr.bytes_out,
                                               cr.tok_in, cr.tok_out, cr.elapsed_ms,
                                               copts.mode == compress::Mode::Aggressive
                                                ? "auto-aggressive" : "auto-lossless");
                    } catch (...) {}
                    int saved_pct = cr.tok_in > 0
                                     ? (100 - (100 * cr.tok_out / cr.tok_in)) : 0;
                    std::cerr << "[icmg pack] auto-compress: "
                              << cr.tok_in << " → " << cr.tok_out
                              << " tok (" << saved_pct << "% saved, "
                              << cr.glossary.size() << " glossary entries)\n";
                    capped = cr.text;
                }
            } catch (...) {}
        }

        // Phase 41 T1+T2: thinking-budget directive.
        // Phase 62: auto-think is now ON BY DEFAULT. Plain `icmg pack "task"`
        // classifies intent + applies --no-think when task is simple. Opt out
        // with --full-think for users who want full thinking pass.
        bool no_think    = hasFlag(args, "--no-think");
        bool concise     = hasFlag(args, "--concise");
        bool sayless     = hasFlag(args, "--sayless");
        bool full_think  = hasFlag(args, "--full-think");
        // Phase 67 hotfix: if global sayless flag set (~/.icmg/sayless.flag),
        // force --no-think + --sayless regardless of classifier. Addresses
        // model thinking-phase directive non-compliance bug — kill thinking
        // outright instead of trying to constrain it via prompt.
        {
            const char* h = std::getenv("USERPROFILE");
            if (!h) h = std::getenv("HOME");
            if (h && !full_think) {
                std::filesystem::path flag = std::filesystem::path(h) / ".icmg" / "sayless.flag";
                if (std::filesystem::exists(flag)) {
                    sayless = true;
                    no_think = true;
                }
            }
        }
        bool auto_think  = hasFlag(args, "--auto-think") ||
                           (!no_think && !concise && !sayless && !full_think);
        cli::Intent classified = cli::Intent::Unknown;
        if (auto_think && !no_think && !concise && !sayless) {
            classified = cli::classifyIntent(task);
            if (classified == cli::Intent::Simple) no_think = true;
            std::cerr << "[icmg pack] intent=" << cli::intentLabel(classified)
                      << (no_think ? " → no-think directive applied"
                                   : " → thinking kept on") << "\n";
        }
        if (sayless)       capped = cli::applySaylessDirective(capped);
        else if (concise)  capped = cli::applyConciseDirective(capped);
        else if (no_think) capped = cli::applyNoThinkDirective(capped);

        // Phase 41 T4: telemetry record.
        // Phase 62: only record when a directive actually applied — skip noise
        // rows that show "0 saved" on the savings dashboard.
        bool any_directive = no_think || concise || sayless;
        if (any_directive) {
            try {
                db.run("INSERT INTO thinking_telemetry (cmd, task, intent, no_think, concise, input_bytes) "
                       "VALUES (?,?,?,?,?,?)",
                       {"pack", task,
                        cli::intentLabel(classified),
                        no_think ? "1" : "0",
                        concise  ? "1" : "0",
                        std::to_string((int)capped.size())});
            } catch (...) {}
        }

        // T6 (v1.4.0): secret scan — warn+redact by default; --allow-secrets skips;
        // --strict-secrets exits 2 so callers can detect secrets in output.
        bool allow_secrets  = hasFlag(args, "--allow-secrets");
        bool strict_secrets = hasFlag(args, "--strict-secrets");
        int  secret_exit_code = 0;
        if (!allow_secrets && !capped.empty()) {
            auto sec_matches = core::scanSecrets(capped);
            if (!sec_matches.empty()) {
                // Build per-type count map for warning message.
                std::map<std::string, int> type_counts;
                for (auto& m : sec_matches) type_counts[m.type]++;
                std::string warn = "pack: " + std::to_string(sec_matches.size())
                                   + " secret(s) redacted: ";
                bool first = true;
                for (auto& kv : type_counts) {
                    if (!first) warn += ", ";
                    warn += kv.first + " x " + std::to_string(kv.second);
                    first = false;
                }
                std::cerr << warn << "\n";
                capped = core::redactSecrets(capped, sec_matches);
                if (strict_secrets) secret_exit_code = 2;
            }
        }

        // Phase 66 T1: differential pack — emit only delta vs last pack
        // saved at .icmg/last-pack.txt. Massive saving when running pack
        // repeatedly during iterative work (typical 60-90% reduction).
        std::filesystem::path last_path = ".icmg/last-pack.txt";
        bool diff_reset = hasFlag(args, "--diff-reset");
        bool diff_mode = hasFlag(args, "--diff") ||
            (!hasFlag(args, "--no-diff") && !diff_reset && std::filesystem::exists(last_path));
        if (diff_reset) {
            std::error_code ec;
            std::filesystem::remove(last_path, ec);
            std::cerr << "[icmg pack] --diff baseline cleared\n";
        }
        if (diff_mode && std::filesystem::exists(last_path)) {
            std::ifstream lf(last_path);
            std::ostringstream lbuf; lbuf << lf.rdbuf();
            std::string prev = lbuf.str();
            std::string delta = computePackDelta(prev, capped);
            std::cout << "# pack-diff vs previous (" << prev.size() << "B → "
                      << delta.size() << "B, "
                      << (prev.empty() ? 0 : 100 - (int)(100 * delta.size() / prev.size()))
                      << "% smaller)\n"
                      << delta;
        } else {
            if (diff_mode) {
                std::cerr << "[icmg pack] no previous pack at " << last_path.string()
                          << " — emitting full pack (next run will diff)\n";
            }
            std::cout << capped;
        }
        // Persist current pack for next --diff call.
        try {
            std::error_code ec;
            std::filesystem::create_directories(".icmg", ec);
            std::ofstream of(last_path, std::ios::binary);
            of << capped;
        } catch (...) {}
        return secret_exit_code;
    }

    // Phase 68 T2: computePackDelta extracted to pack_delta.hpp for unit tests.
};

// =============================================================================
// icmg diff-summary
// =============================================================================

class DiffSummaryCommand : public BaseCommand {
public:
    std::string name()        const override { return "diff-summary"; }
    std::string description() const override { return "Symbol-aware git diff summary"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg diff-summary [--ref REF] [--full] [--limit N]\n\n"
            "Wraps `git diff [REF]` and groups changes by enclosing symbol\n"
            "(via line_start/line_end of indexed graph nodes).\n\n"
            "Options:\n"
            "  --ref REF      Compare against REF (default: working tree vs index)\n"
            "  --full         Append raw `git diff` output\n"
            "  --limit N      Cap files printed (default 200) on huge changesets\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        std::string ref = flagValue(args, "--ref");
        bool full = hasFlag(args, "--full");
        // Phase 64: cap file count on huge working trees (default 200).
        int limit = 200;
        try { limit = std::stoi(flagValue(args, "--limit", "200")); } catch (...) {}
        // v1.44.0 B2: compact mode strips whitespace+blank-line churn.
        bool compact = hasFlag(args, "--compact");

        // Phase 64: single git-diff with --unified=0.
        std::string raw_cmd = "git diff --unified=0";
        if (compact) raw_cmd += " --ignore-all-space --ignore-blank-lines";
        if (!ref.empty()) raw_cmd += " " + ref;
        auto result = core::safeExec({"sh", "-c", raw_cmd}, true, 60000);
        if (result.exit_code != 0) {
            std::cerr << "git diff failed: " << result.out << "\n";
            return 1;
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        std::cout << "Diff summary";
        if (!ref.empty()) std::cout << " (ref=" << ref << ")";
        std::cout << ":\n";

        // Single-pass parse: scan for `diff --git a/<path> b/<path>` headers,
        // collect `@@ ... +<start>,<len> @@` hunks per file.
        std::regex file_re(R"(^diff --git a/(.+?) b/(.+)$)");
        std::regex hunk_re(R"(^@@\s+-\d+(?:,\d+)?\s+\+(\d+)(?:,(\d+))?\s+@@)");
        std::istringstream iss(result.out);
        std::string line;
        std::string cur_file;
        std::vector<std::pair<int,int>> cur_ranges;
        int file_count = 0;
        bool truncated = false;

        auto flushFile = [&]() {
            if (cur_file.empty()) return;
            ++file_count;
            if (file_count > limit) { truncated = true; return; }
            std::cout << "M " << cur_file << "\n";
            for (auto& [s, e] : cur_ranges) {
                std::string sym, kind;
                db.query(
                    "SELECT COALESCE(symbol_name,''), kind FROM graph_nodes"
                    " WHERE path=? AND kind != 'file'"
                    "   AND line_start <= ? AND line_end >= ?"
                    " ORDER BY (line_end - line_start) ASC LIMIT 1",
                    {cur_file, std::to_string(s), std::to_string(e)},
                    [&](const core::Row& r){ if (r.size() >= 2) { sym = r[0]; kind = r[1]; } });
                if (!sym.empty())
                    std::cout << "  ~ " << kind << " " << sym << " (L" << s << "-" << e << ")\n";
                else
                    std::cout << "  ~ L" << s << "-" << e << "\n";
            }
        };

        while (std::getline(iss, line)) {
            while (!line.empty() && (line.back() == '\r')) line.pop_back();
            std::smatch m;
            if (std::regex_match(line, m, file_re)) {
                flushFile();
                cur_file = m[2].str();  // prefer b/ path (post-rename)
                cur_ranges.clear();
            } else if (std::regex_search(line, m, hunk_re)) {
                int start = std::stoi(m[1].str());
                int len = m[2].matched ? std::stoi(m[2].str()) : 1;
                if (len == 0) len = 1;
                cur_ranges.push_back({start, start + len - 1});
            }
        }
        flushFile();

        if (file_count == 0) std::cout << "  (no changes)\n";
        if (truncated) {
            std::cout << "  ... " << (file_count - limit) << " more file(s) truncated. "
                      << "Raise --limit to see all.\n";
        }

        if (full) {
            std::cout << "\n--- Full diff ---\n";
            std::string raw_full = "git diff" + (ref.empty() ? std::string() : " " + ref);
            auto raw = core::safeExec({"sh", "-c", raw_full}, true, 60000);
            std::cout << raw.out;
        }
        return 0;
    }
};

// =============================================================================
// icmg explain <error>
// =============================================================================

class ExplainCommand : public BaseCommand {
public:
    std::string name()        const override { return "explain"; }
    std::string description() const override { return "Match error against errors-resolved memory"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { std::cerr << "icmg explain: requires <error-text>\n"; return 1; }
        std::string err;
        for (auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            if (!err.empty()) err += " ";
            err += a;
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        // Recall scoped to errors-resolved topic prefix + BM25 search by error tokens
        auto candidates = mem.recallByTopic("errors-resolved", 50);
        std::string err_lo = err;
        std::transform(err_lo.begin(), err_lo.end(), err_lo.begin(), ::tolower);
        int matched = 0;
        for (auto& n : candidates) {
            std::string tlo = n.topic;
            std::transform(tlo.begin(), tlo.end(), tlo.begin(), ::tolower);
            std::string pat = tlo.size() > 17 ? tlo.substr(17) : "";
            if (pat.empty()) continue;
            // Extract first 3 tokens of pattern
            std::istringstream iss(pat);
            std::string tok;
            int ok_tokens = 0, total = 0;
            while (std::getline(iss, tok, ' ') && total < 4) {
                if (tok.size() < 3) continue;
                ++total;
                if (err_lo.find(tok) != std::string::npos) ++ok_tokens;
            }
            if (total > 0 && ok_tokens >= (total + 1) / 2) {
                std::cout << "Past resolution #" << n.id << ":\n  " << n.topic << "\n  "
                          << trunc(n.content, 200) << "\n\n";
                if (++matched >= 3) break;
            }
        }
        if (matched == 0) std::cout << "No matching past resolution. Use `icmg known-issue add` to register one.\n";
        return 0;
    }
};

// =============================================================================
// icmg session save / restore
// =============================================================================

class SessionCommand : public BaseCommand {
public:
    std::string name()        const override { return "session"; }
    std::string description() const override { return "Snapshot active task context"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg session <subcommand>\n\n"
            "Subcommands:\n"
            "  save <name>    Snapshot recent recalls + open files\n"
            "  restore <name> Re-emit snapshot bundle\n"
            "  list           Show saved sessions\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        if (sub == "save") {
            if (rest.empty()) { std::cerr << "session save: requires <name>\n"; return 1; }
            std::string name = rest[0];
            // Snapshot = recent query history + freshly recalled context
            auto qs = mem.queryHistory(20);
            std::ostringstream content;
            content << "Session: " << name << "\n"
                    << "Saved at: " << bndNow() << "\n\n"
                    << "Recent queries (" << qs.size() << "):\n";
            for (auto& q : qs) content << "- " << q << "\n";

            imem::MemoryNode n;
            n.topic    = "session-snapshot " + name;
            n.content  = content.str();
            n.keywords = "session snapshot " + name;
            n.importance = 1;
            try { int64_t id = mem.store(n, /*force=*/true); std::cout << "Saved session #" << id << ": " << name << "\n"; }
            catch (...) { std::cerr << "save failed\n"; return 1; }
            return 0;
        }
        if (sub == "restore") {
            if (rest.empty()) { std::cerr << "session restore: requires <name>\n"; return 1; }
            auto items = mem.recallByTopic("session-snapshot " + rest[0], 1);
            if (items.empty()) { std::cerr << "session not found: " << rest[0] << "\n"; return 1; }
            std::cout << items[0].content;
            return 0;
        }
        if (sub == "list") {
            auto items = mem.recallByTopic("session-snapshot", 50);
            for (auto& n : items) std::cout << "#" << n.id << "  " << n.topic << "\n";
            return 0;
        }
        std::cerr << "icmg session: unknown subcommand: " << sub << "\n";
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("context",      ContextCommand);
ICMG_REGISTER_COMMAND("pack",         PackCommand);
ICMG_REGISTER_COMMAND("diff-summary", DiffSummaryCommand);
ICMG_REGISTER_COMMAND("explain",      ExplainCommand);
ICMG_REGISTER_COMMAND("session",      SessionCommand);

// =============================================================================
// context-delta: show changed lines in a file since last git HEAD.

class ContextDeltaCommand : public BaseCommand {
public:
    std::string name()        const override { return "context-delta"; }
    std::string description() const override {
        return "Show changed lines in file since HEAD (git diff wrapper)";
    }
    void usage() const override {
        std::cout <<
            "Usage: icmg context-delta <file> [--ref REF]\n\n"
            "  Show only the changed lines in <file> since last git commit.\n"
            "  Useful for re-reads: instead of reading full file, see what changed.\n\n"
            "Options:\n"
            "  --ref REF   Compare against REF (default: HEAD)\n";
    }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string file;
        for (auto& a : args) {
            if (!a.empty() && a[0] != '-') { file = a; break; }
        }
        if (file.empty()) { std::cerr << "icmg context-delta: requires <file>\n"; return 1; }
        std::string ref = flagValue(args, "--ref", "HEAD");
        std::string cmd = "git diff " + ref + " -- \"" + file + "\" 2>/dev/null";
        auto r = core::safeExecShell(cmd, true, 10000);
        if (r.exit_code != 0 || r.out.empty()) {
            std::cout << "No changes in " << file << " since " << ref << ".\n";
        } else {
            std::cout << r.out;
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("context-delta", ContextDeltaCommand);

} // namespace icmg::cli
