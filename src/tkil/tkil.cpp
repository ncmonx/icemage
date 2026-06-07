#include "../core/token_counter.hpp"
#include "tkil.hpp"
#include "runner.hpp"
#include "filters/filter_utils.hpp"
#include "ultra_pipeline.hpp"   // v1.56 T1
#include "filters/wasm_filter.hpp"   // v2.x WASM skill filter
#include "../wasm/wasm_registry.hpp"
#include "../core/registry.hpp"
#include "../core/turn_cache.hpp"
#include "../core/posix_compat.hpp"  // v1.41.x MSVC popen/pclose shim
#include <cctype>
#include <filesystem>
// v1.40.2 C++23 std::flat_map pilot (P0429). Header guarded — Linux WSL
// libstdc++ may be older (< 15). When unavailable, fall back to unordered_map.
#if __has_include(<flat_map>)
#  include <flat_map>
#  define ICMG_HAS_FLAT_MAP 1
#else
#  define ICMG_HAS_FLAT_MAP 0
#endif
#include <fstream>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace icmg::tkil {

static int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

Tkil::Tkil(core::Db& db) : db_(db) {}

BaseFilter* Tkil::getFilter(CmdType type) const {
    // v1.40.2 C++23 std::flat_map adoption (P0429). Cache-friendly for small
    // N. Fallback to unordered_map when libstdc++ <15 (Linux CI WSL).
#if ICMG_HAS_FLAT_MAP
    static const std::flat_map<CmdType, std::string> type2key = {
#else
    static const std::unordered_map<CmdType, std::string> type2key = {
#endif
        {CmdType::GitLog,         "git"},
        {CmdType::Build,          "build"},
        {CmdType::Test,           "test"},
        {CmdType::Search,         "search"},
        {CmdType::Docker,         "build"},  // reuse build filter
        {CmdType::PackageManager, "npm"},
        {CmdType::Db,             "db"},
        {CmdType::Vitest,         "vitest"},
        {CmdType::Playwright,     "playwright"},
        {CmdType::Tsc,            "tsc"},
        {CmdType::Lint,           "lint"},
        {CmdType::Logs,           "log-dedup"},  // v1.20.4 (F6)
        {CmdType::Rust,           "rust"},       // v1.21.3 (F3)
        {CmdType::Go,             "go"},
        {CmdType::Java,           "java"},
        {CmdType::Dotnet,         "dotnet"},
        {CmdType::Swift,          "swift"},
        {CmdType::Kotlin,         "kotlin"},
        {CmdType::Default,        "default"},
    };
    auto& reg = core::Registry<icmg::tkil::BaseFilter>::instance();
    auto it = type2key.find(type);
    std::string key = (it != type2key.end()) ? it->second : "default";
    if (reg.has(key)) {
        // Filters are stateless — safe to return raw pointer from static storage
        static std::unordered_map<std::string, std::unique_ptr<BaseFilter>> cache;
        if (cache.find(key) == cache.end())
            cache[key] = reg.create(key);
        return cache[key].get();
    }
    return nullptr;
}

int Tkil::runFiltered(const std::string& command, bool raw, bool json,
                     bool dry_run, bool stream, bool ultra) {
    CmdType type = detector_.detect(command);

    // A7: dry-run mode
    if (dry_run) {
        static const std::string type_names[] = {
            "GitLog","Build","Test","Search","Docker","PackageManager","Db","Default"};
        int idx = (int)type;
        std::string tname = (idx >= 0 && idx < 8) ? type_names[idx] : "Default";
        std::cout << "Command: " << command << "\n"
                  << "Detected type: " << tname << "\n"
                  << "Filter: " << tname << "Filter\n"
                  << "[To execute: icmg run " << command << "]\n";
        return 0;
    }

    // Phase 82 T5: real-time streaming — print each line as subprocess emits it.
    // Filter summary appended at end so filter context (full output) is preserved.
    if (stream && !json) {
        auto argv = parseArgv(command);
        std::string sh_cmd;
        for (size_t i = 0; i < argv.size(); ++i) {
            if (i) sh_cmd += ' ';
            const auto& a = argv[i];
            bool needs_q = a.empty() ||
                           a.find_first_of(" \t\"'") != std::string::npos;
            if (needs_q) { sh_cmd += '"'; sh_cmd += a; sh_cmd += '"'; }
            else sh_cmd += a;
        }
        sh_cmd += " 2>&1";
        if (FILE* pipe = popen(sh_cmd.c_str(), "r")) {
            std::string all_out;
            char buf[4096];
            while (fgets(buf, sizeof(buf), pipe)) {
                std::string line(buf);
                std::cout << line;
                std::cout.flush();
                all_out += line;
            }
            pclose(pipe);
            if (!raw) {
                auto* f = getFilter(type);
                if (f) {
                    auto fr2 = f->filter(all_out, command);
                    if (fr2.original_lines != fr2.filtered_lines)
                        std::cout << "[stream: " << fr2.filtered_lines << "/"
                                  << fr2.original_lines << " lines pass filter]\n";
                }
            }
            int nlines = (int)splitLines(all_out).size();
            recordCommand(command, nlines, nlines);
            return 0;
        }
        // popen failed — fall through to buffered path
    }

    // Execute (A1: argv-safe via parseArgv + safeExec)
    auto result = runCommand(command, /*merge_stderr=*/true);

    std::string combined = result.out;
    if (!result.err.empty() && result.out.empty()) combined = result.err;

    FilterResult fr;
    if (raw) {
        fr.output         = combined;
        fr.original_lines = (int)splitLines(combined).size();
        fr.filtered_lines = fr.original_lines;
    } else {
        auto* f = getFilter(type);
        if (f) {
            // v1.44.0 B1: RTK-style tee fallback. If filter throws OR
            // collapses output to <5% of input (parse fail suspect), retain
            // raw to prevent data loss. Opt-out: ICMG_NO_TEE_FALLBACK=1.
            try {
                fr = f->filter(combined, command);
                bool opt_out = std::getenv("ICMG_NO_TEE_FALLBACK") != nullptr;
                if (!opt_out && combined.size() > 256
                    && fr.output.size() * 20 < combined.size()) {
                    // suspect filter mis-detection: keep raw
                    fr.output         = combined;
                    fr.original_lines = (int)splitLines(combined).size();
                    fr.filtered_lines = fr.original_lines;
                    fr.notes         += " [tee-fallback]";
                }
            } catch (const std::exception& e) {
                fr.output         = combined;
                fr.original_lines = (int)splitLines(combined).size();
                fr.filtered_lines = fr.original_lines;
                fr.notes          = std::string("filter-threw: ") + e.what();
            }
        } else {
            fr.output         = combined;
            fr.original_lines = (int)splitLines(combined).size();
            fr.filtered_lines = fr.original_lines;
        }
    }

    // v1.21.1 (F2): apply user-supplied per-project filter rules from
    // .icmg/filters.toml on top of the built-in filter. No-op when file
    // is absent. Rules are first-match-wins; matching command runs all
    // strip patterns against each line.
    {
        // v1.41.x: applyUserFilters declared in tkil.hpp (namespace
        // icmg::tkil) for proper MSVC name lookup. Definition in
        // regex_user_filter.cpp.
        std::string after = applyUserFilters(fr.output, command);
        if (after.size() != fr.output.size()) {
            fr.output = after;
            fr.filtered_lines = (int)splitLines(after).size();
        }
    }
    // v1.56 T1: Tkil Ultra pipeline (Stages 2-5). Applies when explicit
    // --ultra flag / env ICMG_TKIL_ULTRA is set OR auto-trigger fires
    // (output > 5 KB AND duplication-ratio > 0.4). Error/fatal lines
    // survive every stage via isAlwaysVerbatim allowlist.
    if (!raw) {
        bool do_ultra = ultra || icmg::tkil::autoTriggerUltra(fr.output);
        if (do_ultra) {
            std::string ultra_out = icmg::tkil::applyUltraPipeline(fr.output, command);
            if (ultra_out.size() != fr.output.size()) {
                fr.output = ultra_out;
                fr.filtered_lines = (int)splitLines(ultra_out).size();
            }
        }
    }


    // v2.x (WASM): apply a registered WASM skill filter whose match fits this
    // command (persona DB zone "wasm"). Fail-open -- any error leaves output
    // unchanged (WasmFilter passes through); must never break `icmg run`.
    if (!raw) {
        try {
            if (auto skill = icmg::wasm::matchWasmSkill(command)) {
                icmg::tkil::WasmFilter wf(*skill);
                auto wr = wf.filter(fr.output, command);
                if (wr.output != fr.output) {
                    fr.output = wr.output;
                    fr.filtered_lines = (int)splitLines(wr.output).size();
                }
            }
        } catch (...) { /* wasm must never break icmg run */ }
    }

    // v1.21.1 (S2): turn_cache wiring for idempotent read-only commands.
    // Wired only for `grep`, `rg`, `git status`, `git diff` (no fs side
    // effects between calls). Returns short ref on repeat-within-TTL.
    // Disabled when ICMG_NO_TURN_CACHE=1.
    {
        bool eligible = false;
        if (command.find("grep ") == 0 || command.find("rg ") == 0 ||
            command.find("git status") == 0 || command.find("git diff") == 0) {
            eligible = true;
        }
        const char* off = std::getenv("ICMG_NO_TURN_CACHE");
        if (eligible && !(off && *off && std::string(off) != "0")) {
            namespace tc = icmg::core::turn_cache;
            auto cached = tc::lookup("tkil-run", command, 0);
            if (!cached.empty()) {
                // Cache hit — replace output with ref pointer.
                fr.output = "[turn_cache] " + cached + "\n"
                          + "(identical to prior invocation; full output suppressed; "
                          + "set ICMG_NO_TURN_CACHE=1 to bypass)\n";
                fr.filtered_lines = 2;
                fr.was_truncated = true;
            } else {
                tc::recordResult("tkil-run", command, 0, fr.output);
            }
        }
    }

    if (json) {
        // Escape output for JSON
        std::string escaped;
        for (char c : fr.output) {
            if      (c == '"')  escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else if (c == '\r') escaped += "\\r";
            else if (c == '\t') escaped += "\\t";
            else                escaped += c;
        }
        std::cout << "{"
            << "\"command\":\"" << command << "\""
            << ",\"exit_code\":" << result.exit_code
            << ",\"duration_ms\":" << result.duration_ms
            << ",\"original_lines\":" << fr.original_lines
            << ",\"filtered_lines\":" << fr.filtered_lines
            << ",\"was_truncated\":" << (fr.was_truncated ? "true" : "false")
            << ",\"output\":\"" << escaped << "\""
            << "}\n";
    } else {
        std::cout << fr.output;
        if (!raw && fr.was_truncated) {
            std::cout << "[" << fr.filtered_lines << "/" << fr.original_lines
                      << " lines shown]\n";
        }
    }

    recordCommand(command, fr.original_lines, fr.filtered_lines);

    // Phase 20: log to tool_invocations for `icmg budget`. Best-effort —
    // ignore failures (table may not exist on legacy DBs that didn't migrate).
    try {
        int64_t raw_b  = (int64_t)combined.size();
        int64_t filt_b = (int64_t)fr.output.size();
        int64_t in_t   = (int64_t)core::countTokens(combined);
        int64_t out_t  = (int64_t)core::countTokens(fr.output);
        int64_t saved  = in_t > out_t ? (in_t - out_t) : 0;
        db_.run(
            "INSERT INTO tool_invocations(tool_name,command,raw_bytes,filtered_bytes,"
            " est_tokens_in,est_tokens_out,saved_tokens) VALUES('run',?,?,?,?,?,?)",
            {command, std::to_string(raw_b), std::to_string(filt_b),
             std::to_string(in_t), std::to_string(out_t), std::to_string(saved)});
    } catch (...) { /* table missing on un-migrated DB — skip */ }

    // v1.21.2 (F1): tee-on-failure spill. When command exited non-zero AND
    // the filter shrunk output significantly (>50%), save the raw stream to
    // `.icmg/spill/<ts>_<cmd>.log` and append a pointer so the agent can
    // expand it on demand. Successful runs skip spill (no loss possible).
    // Tunable: ICMG_NO_SPILL=1 to disable; ICMG_SPILL_SHRINK_PCT (default 50).
    if (result.exit_code != 0 && fr.was_truncated
        && !std::getenv("ICMG_NO_SPILL")) {
        int min_shrink_pct = 50;
        const char* env_pct = std::getenv("ICMG_SPILL_SHRINK_PCT");
        if (env_pct) { try { min_shrink_pct = std::stoi(env_pct); } catch (...) {} }
        int actual_pct = fr.original_lines > 0
            ? (int)(100 - (100.0 * fr.filtered_lines / fr.original_lines)) : 0;
        if (actual_pct >= min_shrink_pct && !combined.empty()) {
            try {
                namespace fs = std::filesystem;
                fs::path spill_dir = fs::path(".icmg") / "spill";
                std::error_code ec;
                fs::create_directories(spill_dir, ec);
                auto ts = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                // Sanitize first cmd token for filename.
                std::string base;
                for (char c : command) {
                    if (base.size() >= 24) break;
                    if (c == ' ' || c == '\t') break;
                    if (std::isalnum((unsigned char)c) || c == '_' || c == '-') base += c;
                }
                if (base.empty()) base = "cmd";
                fs::path spill = spill_dir / (std::to_string(ts) + "_" + base + ".log");
                std::ofstream sf(spill, std::ios::binary);
                if (sf) {
                    sf << combined;
                    sf.close();
                    if (!json) {
                        std::cerr << "[F1 tee-on-failure] full raw stream saved to "
                                  << spill.string() << " (exit=" << result.exit_code
                                  << ", filter shrink=" << actual_pct << "%)\n";
                    }
                }
            } catch (...) { /* spill best-effort */ }
        }
    }

    return result.exit_code;
}

void Tkil::recordCommand(const std::string& cmd, int orig, int filt) {
    int64_t now = nowEpoch();
    // Upsert: bump frequency + update last_used + accumulate line stats
    db_.run(
        "INSERT INTO commands(command,frequency,last_used,total_original_lines,total_filtered_lines)"
        " VALUES(?,1,?,?,?)"
        " ON CONFLICT(command) DO UPDATE SET"
        " frequency=frequency+1,"
        " last_used=excluded.last_used,"
        " total_original_lines=total_original_lines+excluded.total_original_lines,"
        " total_filtered_lines=total_filtered_lines+excluded.total_filtered_lines",
        {cmd, std::to_string(now), std::to_string(orig), std::to_string(filt)});
}

void Tkil::recordManual(const std::string& cmd) {
    recordCommand(cmd, 0, 0);
}

double Tkil::computeScore(int freq, int64_t last_used) const {
    int64_t now = nowEpoch();
    double hours = (now - last_used) / 3600.0;
    double recency = std::exp(-0.02 * hours);  // decay faster than memory
    return freq * recency * 100.0;
}

std::vector<CmdRecord> Tkil::suggest(const std::string& prefix, int limit) {
    std::vector<CmdRecord> result;
    std::string pat = prefix.empty() ? "%" : prefix + "%";

    db_.query(
        "SELECT id,command,frequency,last_used,"
        "COALESCE(total_original_lines,0),COALESCE(total_filtered_lines,0)"
        " FROM commands WHERE command LIKE ? ORDER BY frequency DESC LIMIT ?",
        {pat, std::to_string(limit * 3)},  // get more, then re-rank by score
        [&](const core::Row& r) {
            CmdRecord rec;
            if (r.size() > 0) try { rec.id = std::stoll(r[0]); } catch (...) {}
            if (r.size() > 1) rec.command = r[1];
            if (r.size() > 2) try { rec.frequency = std::stoi(r[2]); } catch (...) {}
            if (r.size() > 3) try { rec.last_used = std::stoll(r[3]); } catch (...) {}
            if (r.size() > 4) try { rec.total_original_lines = std::stoi(r[4]); } catch (...) {}
            if (r.size() > 5) try { rec.total_filtered_lines = std::stoi(r[5]); } catch (...) {}
            rec.score = computeScore(rec.frequency, rec.last_used);
            result.push_back(rec);
        });

    // Re-sort by score
    std::stable_sort(result.begin(), result.end(),
        [](const CmdRecord& a, const CmdRecord& b) { return a.score > b.score; });
    if ((int)result.size() > limit) result.resize(limit);
    return result;
}

void Tkil::printStats() const {
    int total_runs = 0;
    int64_t total_orig = 0, total_filt = 0;

    db_.query("SELECT SUM(frequency), SUM(total_original_lines), SUM(total_filtered_lines) FROM commands",
              {},
              [&](const core::Row& r) {
                  if (r.size() > 0) try { total_runs = std::stoi(r[0]); } catch (...) {}
                  if (r.size() > 1) try { total_orig = std::stoll(r[1]); } catch (...) {}
                  if (r.size() > 2) try { total_filt = std::stoll(r[2]); } catch (...) {}
              });

    double avg_reduction = total_orig > 0
        ? (1.0 - (double)total_filt / total_orig) * 100.0 : 0.0;
    double est_tokens_saved = (total_orig - total_filt) * 4.0 / 1000.0; // ~4 chars/token

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Total runs:     " << total_runs << "\n";
    std::cout << "Avg reduction:  " << avg_reduction << "%\n";
    std::cout << "Est. tokens saved: ~" << (int)(est_tokens_saved) << "K\n";

    // Top savers
    std::cout << "\nTop savers:\n";
    db_.query(
        "SELECT command, frequency,"
        " CASE WHEN total_original_lines>0 THEN"
        "  ROUND((1.0-CAST(total_filtered_lines AS REAL)/total_original_lines)*100)"
        " ELSE 0 END AS reduction"
        " FROM commands WHERE total_original_lines > 0"
        " ORDER BY reduction DESC LIMIT 5",
        {},
        [](const core::Row& r) {
            if (r.size() < 3) return;
            std::cout << "  " << std::left;
            std::cout.width(30); std::cout << r[0];
            std::cout << r[1] << "x  " << r[2] << "% reduction\n";
        });
}

} // namespace icmg::tkil
