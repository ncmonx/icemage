// `icmg shrink` — auto-detect content type → route to best shrink strategy.
//
// Used by PostToolUse Bash hook when stdout exceeds threshold. Returns
// token-friendly representation while preserving the signal.
//
// Detection heuristics:
//   - grep/find output (path:line pattern)        → keep matches, group by file, cap
//   - build/test logs (errors/warnings)           → keep error blocks + summary
//   - SQL/table dumps (header + N rows)           → header + first 20 + last 5
//   - generic large text                          → semantic compress with glossary
//   - structured JSON                             → keep keys, abbreviate values
//   - default                                     → head + tail with byte count

#include "../base_command.hpp"
#include "../../core/compress_select.hpp"  // v2.0.0 TE2 salience
#include "../../core/dangling_guard.hpp"    // 2026-09-07 C: dangling-ref repair
#include "../../core/registry.hpp"
#include "../../core/stdin_util.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../compress/compressor.hpp"
#include "../../compress/glossary_store.hpp"
#include "../../llm/llama_runner.hpp"   // LlamaRunner::available() + isLoaded()

#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

namespace {

enum class Kind { Grep, BuildLog, SqlTable, Json, Generic };

Kind detect(const std::string& s) {
    // Sample first 4KB.
    std::string head = s.substr(0, std::min<size_t>(4096, s.size()));

    // Grep pattern: path:line:content on most lines.
    int grep_lines = 0, total_lines = 0;
    {
        std::istringstream is(head);
        std::string line;
        std::regex re(R"(^[^:]+:\d+:)");
        while (std::getline(is, line) && total_lines < 100) {
            ++total_lines;
            if (std::regex_search(line, re)) ++grep_lines;
        }
        if (total_lines > 0 && grep_lines * 2 > total_lines) return Kind::Grep;
    }

    // Build log: error/warning markers
    if (head.find("error:") != std::string::npos
     || head.find("Error:") != std::string::npos
     || head.find("FAIL") != std::string::npos
     || head.find("undefined reference") != std::string::npos) {
        return Kind::BuildLog;
    }

    // SQL table: header line with multiple `|` or `+---+`
    if (head.find("+---") != std::string::npos
     || head.find("---+") != std::string::npos
     || (head.find("|") != std::string::npos && head.find("|---") != std::string::npos)) {
        return Kind::SqlTable;
    }

    // JSON: starts with { or [
    for (char c : head) {
        if (c == ' ' || c == '\t' || c == '\n') continue;
        if (c == '{' || c == '[') return Kind::Json;
        break;
    }

    return Kind::Generic;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string line;
    while (std::getline(is, line)) out.push_back(line);
    return out;
}

// Grep: group by file, cap matches per file.
std::string shrinkGrep(const std::string& s, int max_matches_per_file = 5) {
    auto lines = splitLines(s);
    std::map<std::string, std::vector<std::string>> by_file;
    std::regex re(R"(^([^:]+):(\d+):(.*))");
    int total = 0;
    for (auto& ln : lines) {
        std::smatch m;
        if (std::regex_match(ln, m, re)) {
            by_file[m[1].str()].push_back(m[2].str() + ": " + m[3].str());
            ++total;
        }
    }
    std::ostringstream os;
    os << "[icmg shrink: grep] " << total << " matches across "
       << by_file.size() << " files\n\n";
    for (auto& [f, ms] : by_file) {
        os << f << " (" << ms.size() << " matches)\n";
        int cap = std::min((int)ms.size(), max_matches_per_file);
        for (int i = 0; i < cap; ++i) os << "  " << ms[i] << "\n";
        if ((int)ms.size() > cap) os << "  ... +" << (ms.size() - cap) << " more\n";
        os << "\n";
    }
    return os.str();
}

// Build log: keep error/warning lines + summary.
std::string shrinkBuildLog(const std::string& s, int context = 2) {
    auto lines = splitLines(s);
    std::ostringstream os;
    int err = 0, warn = 0;
    std::vector<std::pair<int,std::string>> kept;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& ln = lines[i];
        bool is_err  = ln.find("error:") != std::string::npos
                    || ln.find("Error:") != std::string::npos
                    || ln.find("FAIL") != std::string::npos
                    || ln.find("undefined reference") != std::string::npos;
        bool is_warn = ln.find("warning:") != std::string::npos
                    || ln.find("Warning:") != std::string::npos;
        if (is_err)  ++err;
        if (is_warn) ++warn;
        if (is_err || is_warn) {
            int lo = std::max(0, (int)i - context);
            int hi = std::min((int)lines.size(), (int)i + context + 1);
            for (int j = lo; j < hi; ++j) kept.emplace_back(j, lines[j]);
        }
    }
    // Dedup kept.
    std::set<int> seen;
    os << "[icmg shrink: build log] " << err << " error(s), "
       << warn << " warning(s) in " << lines.size() << " lines\n\n";
    int last_idx = -10;
    for (auto& [i, ln] : kept) {
        if (seen.count(i)) continue;
        seen.insert(i);
        if (i > last_idx + 1 && last_idx >= 0) os << "  ...\n";
        os << "  " << ln << "\n";
        last_idx = i;
    }
    return os.str();
}

// SQL table: first 20 rows + last 5 + count.
std::string shrinkSqlTable(const std::string& s) {
    auto lines = splitLines(s);
    if ((int)lines.size() <= 30) return s;
    std::ostringstream os;
    os << "[icmg shrink: SQL table] " << lines.size() << " rows total\n\n";
    for (int i = 0; i < 20 && i < (int)lines.size(); ++i)
        os << lines[i] << "\n";
    os << "  ... +" << (lines.size() - 25) << " rows truncated ...\n";
    for (int i = (int)lines.size() - 5; i < (int)lines.size(); ++i)
        os << lines[i] << "\n";
    return os.str();
}

// JSON: keep structure, abbreviate long string values.
std::string shrinkJson(const std::string& s) {
    if (s.size() < 8192) return s;
    std::string out;
    out.reserve(s.size() / 2);
    bool in_str = false;
    char prev = 0;
    int str_chars = 0;
    bool truncated_this_str = false;
    const int STR_CAP = 80;
    for (char c : s) {
        if (c == '"' && prev != '\\') {
            in_str = !in_str;
            if (in_str) { str_chars = 0; truncated_this_str = false; }
            out.push_back(c);
        } else if (in_str) {
            ++str_chars;
            if (str_chars <= STR_CAP) out.push_back(c);
            else if (!truncated_this_str) { out += "...["; out += std::to_string(str_chars); out += "ch]"; truncated_this_str = true; }
        } else {
            out.push_back(c);
        }
        prev = c;
    }
    return "[icmg shrink: JSON] string values capped at " + std::to_string(STR_CAP) + " chars\n\n" + out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Aggressive mode helpers (--aggressive flag)
// ─────────────────────────────────────────────────────────────────────────────

// Token abbreviation table: long C++/TS tokens → compact aliases.
static const std::pair<const char*, const char*> kAbbrevTable[] = {
    { "std::string",        "str"   },
    { "std::vector",        "vec"   },
    { "std::unordered_map", "umap"  },
    { "std::map",           "smap"  },
    { "std::optional",      "opt"   },
    { "std::unique_ptr",    "uptr"  },
    { "std::shared_ptr",    "sptr"  },
    { "interface",          "iface" },
    { "function",           "fn"    },
    { "return",             "ret"   },
};

// Abbreviate long tokens on one line.
// For keyword-style tokens (no ':' / '_') require word boundaries so we
// don't mangle identifiers like `returnValue` or `interfaceImpl`.
static std::string aggressiveLine(std::string s) {
    for (auto& [from_raw, to_raw] : kAbbrevTable) {
        std::string from(from_raw), to(to_raw);
        bool need_wb = (from.find(':') == std::string::npos
                     && from.find('_') == std::string::npos);
        std::string::size_type pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            if (need_wb) {
                bool ok_before = (pos == 0
                               || !std::isalnum((unsigned char)s[pos - 1]));
                bool ok_after  = (pos + from.size() >= s.size()
                               || !std::isalnum((unsigned char)s[pos + from.size()])
                               || s[pos + from.size()] == '_');
                if (!ok_before || !ok_after) { pos += from.size(); continue; }
            }
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    return s;
}

// Aggressive shrink pass — applied after kind routing:
//   1. Drop comment-only lines  (//.*)
//   2. Collapse consecutive blank lines to one
//   3. Abbreviate common long tokens via kAbbrevTable
// tok_in / tok_out receive before/after token estimates (bytes/4).
static std::string aggressiveShrink(const std::string& s,
                                    size_t& tok_in, size_t& tok_out) {
    tok_in = s.size() / 4;
    auto lines = splitLines(s);
    std::ostringstream os;
    bool last_blank = false;
    std::regex comment_re(R"(^\s*//.*$)");
    for (auto& ln : lines) {
        // 1. Drop pure comment lines (//.*).
        if (std::regex_match(ln, comment_re)) continue;
        // 3. Abbreviate long tokens.
        std::string proc = aggressiveLine(ln);
        // 2. Collapse consecutive blank lines.
        bool is_blank = proc.find_first_not_of(" \t\r") == std::string::npos;
        if (is_blank) {
            if (last_blank) continue;
            last_blank = true;
        } else {
            last_blank = false;
        }
        os << proc << "\n";
    }
    std::string out = os.str();
    tok_out = out.size() / 4;
    return out;
}

// Generic: head + tail + byte count.
std::string shrinkGeneric(const std::string& s, int head_b = 4096, int tail_b = 2048) {
    if ((int)s.size() <= head_b + tail_b) return s;
    std::ostringstream os;
    os << "[icmg shrink: generic] " << s.size() << " bytes total\n\n";
    os.write(s.data(), head_b);
    os << "\n... [truncated " << (s.size() - head_b - tail_b) << " bytes] ...\n";
    os.write(s.data() + s.size() - tail_b, tail_b);
    return os.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Llama logprob scorer (--scorer=llama)
// ─────────────────────────────────────────────────────────────────────────────

// Compute a salience score for `line` via llama log-probability (A3, wired
// 2026-07-01). Mean surprisal (-log p) over the line's tokens = its information
// content; higher = more surprising/informative = keep. `runner` must be a
// loaded LlamaRunner. On any failure (null runner, empty surprisals) this falls
// back to infoScore() so the caller never breaks.
static double llamaLogprobScore(const std::string& line,
                                icmg::llm::LlamaRunner* runner) {
    if (runner && runner->isLoaded()) {
        auto surp = runner->tokenSurprisals(line);
        if (!surp.empty()) {
            // Mean surprisal, skipping index 0 (no left context) when possible.
            double sum = 0.0; size_t cnt = 0;
            for (size_t i = (surp.size() > 1 ? 1 : 0); i < surp.size(); ++i) {
                sum += surp[i]; ++cnt;
            }
            if (cnt > 0) return sum / (double)cnt;
        }
    }
    // Fallback: heuristic infoScore (model-free, always available).
    return core::infoScore(line);
}

// Resolve + load the active local model into `runner`. Returns true if loaded
// (caller can use llama scoring); false means no model / disabled / load fail
// (caller falls back to heuristic). Same resolution as `icmg ask --backend=local`.
static bool loadActiveModel(icmg::llm::LlamaRunner& runner) {
    if (!icmg::llm::LlamaRunner::available()) return false;
    namespace fs = std::filesystem;
    const char* home =
#ifdef _WIN32
        std::getenv("USERPROFILE");
#else
        std::getenv("HOME");
#endif
    fs::path lldir = (home && *home ? fs::path(home) : fs::current_path()) / ".icmg" / "llm";
    std::error_code ec;
    if (fs::exists(lldir / "disabled", ec)) return false;
    std::string active;
    { std::ifstream af(lldir / "active"); std::getline(af, active); }
    if (active.empty()) return false;
    fs::path gguf = lldir / active / "model.gguf";
    if (!fs::exists(gguf, ec)) return false;
    return runner.load(gguf.string());
}

} // namespace

class ShrinkCommand : public BaseCommand {
public:
    std::string name()        const override { return "shrink"; }
    std::string description() const override {
        return "Auto-detect content type and shrink large output intelligently";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg shrink [<file>]\n"
"       <command> | icmg shrink\n\n"
"Detects content type and applies the best shrink strategy:\n"
"  - grep/find output → group by file, cap matches\n"
"  - build/test logs  → keep errors+warnings with context\n"
"  - SQL/table dumps  → first 20 + last 5 rows\n"
"  - JSON             → cap long string values\n"
"  - generic text     → head + tail + byte count\n\n"
"Options:\n"
"  --kind <K>         Force kind (grep|build|sql|json|generic|compress|salience)\n"
"  --threshold N      Skip if input < N bytes (default 8192)\n"
"  --aggressive       After kind routing: drop comment lines (//.*),\n"
"                     collapse blank lines, abbreviate common long tokens\n"
"                     (std::string->str, std::vector->vec, return->ret,\n"
"                     function->fn, interface->iface, ...) and report\n"
"                     before/after token estimate on stderr.\n"
"  --scorer=llama     (salience mode only) Score lines via llama log-probability\n"
"  --no-dangling-guard (salience) Skip the dangling-reference repair pass\n"
"                     (1.0/(perplexity+1)) instead of the default heuristic.\n"
"                     Requires a loaded LlamaRunner; falls back to infoScore\n"
"                     with a warning when llama is unavailable or not loaded.\n"
"  --json             Emit JSON metadata wrapper\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }

        bool aggressive = hasFlag(args, "--aggressive");

        // --scorer=llama: use llama log-probability scoring in salience mode.
        std::string scorer_arg = flagValue(args, "--scorer");
        bool use_llama_scorer = (scorer_arg == "llama");

        // Read input.
        std::string input;
        std::string path;
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a == "--kind" || a == "--threshold") { ++i; continue; }
            if (a == "--scorer") { ++i; continue; }           // consumed above
            if (!a.empty() && a[0] == '-') continue;          // other flags incl. --scorer=llama
            path = a;
        }
        if (!path.empty()) {
            std::ifstream f(path, std::ios::binary);
            if (!f) { std::cerr << "icmg shrink: open " << path << " failed\n"; return 1; }
            std::ostringstream ss; ss << f.rdbuf();
            input = ss.str();
        } else {
            input = core::slurpStdinSafe();  // isatty-guarded: no-pipe invoke returns instead of hanging
        }

        int threshold = 8192;
        try { auto v = flagValue(args, "--threshold"); if (!v.empty()) threshold = std::stoi(v); } catch (...) {}

        if ((int)input.size() <= threshold) {
            std::cout << input;
            return 0;
        }

        std::string forced = flagValue(args, "--kind");
        Kind k;
        if      (forced == "grep")     k = Kind::Grep;
        else if (forced == "build")    k = Kind::BuildLog;
        else if (forced == "sql")      k = Kind::SqlTable;
        else if (forced == "json")     k = Kind::Json;
        else if (forced == "generic")  k = Kind::Generic;
        else if (forced == "salience") {
            // v2.0.0 TE2: salience backend — keep the most informative lines within
            // the byte budget (threshold). Score is pluggable: heuristic infoScore
            // (default) or llama-logprob perplexity via --scorer=llama (A3, opt-in;
            // falls back to infoScore if no model / load fails).
            auto lines = splitLines(input);
            icmg::llm::LlamaRunner runner;
            bool llama_ok = use_llama_scorer && loadActiveModel(runner);
            if (use_llama_scorer && !llama_ok)
                std::cerr << "[icmg shrink: salience] --scorer=llama unavailable "
                             "(no active model / load failed); using heuristic.\n";
            std::vector<double> scores; scores.reserve(lines.size());
            for (auto& ln : lines)
                scores.push_back(llama_ok ? llamaLogprobScore(ln, &runner)
                                          : core::infoScore(ln));
            // 2026-09-07 C (arXiv 2608.04569): repair dangling references --
            // pull back dropped lines that DEFINE entities the kept lines use.
            auto keep = core::selectMaskByBudget(lines, scores, (size_t)threshold, "\n");
            size_t pulled = 0;
            if (!hasFlag(args, "--no-dangling-guard")) {
                for (size_t idx : core::danglingRepairLines(lines, keep)) {
                    if (!keep[idx]) { keep[idx] = true; ++pulled; }
                }
            }
            std::string out = core::joinKept(lines, keep, "\n");
            std::cout << out << "\n";
            std::cerr << "[icmg shrink: salience" << (llama_ok ? "/llama" : "") << "] "
                      << input.size() << "->" << out.size()
                      << " bytes (" << (input.size() > 0 ? 100 - 100 * out.size() / input.size() : 0)
                      << "% saved" << (pulled ? ", +" + std::to_string(pulled) + " dangling-ref line(s) restored" : "")
                      << ")\n";
            return 0;
        }
        else if (forced == "compress") {
            // Route through semantic compressor.
            compress::CompressOptions opts;
            opts.threshold_tok = 0;
            compress::Compressor c(opts);
            auto r = c.compress(input, ".log");
            std::cout << r.text;
            std::cerr << "[icmg shrink: compress] " << r.tok_in << "→" << r.tok_out
                      << " tok (" << (r.tok_in > 0 ? 100 - 100 * r.tok_out / r.tok_in : 0)
                      << "% saved)\n";
            try {
                core::Db db(core::Config::instance().projectDbPath("."));
                compress::GlossaryStore store(db);
                store.recordTelemetry("shrink", r.bytes_in, r.bytes_out,
                                      r.tok_in, r.tok_out, r.elapsed_ms, "compress");
            } catch (...) {}
            return 0;
        }
        else                            k = detect(input);

        std::string out;
        const char* label = "";
        switch (k) {
            case Kind::Grep:     out = shrinkGrep(input);     label = "grep"; break;
            case Kind::BuildLog: out = shrinkBuildLog(input); label = "build"; break;
            case Kind::SqlTable: out = shrinkSqlTable(input); label = "sql";  break;
            case Kind::Json:     out = shrinkJson(input);     label = "json"; break;
            case Kind::Generic:  out = shrinkGeneric(input);  label = "generic"; break;
        }
        // --aggressive: drop comments, collapse blanks, abbreviate tokens.
        if (aggressive) {
            size_t tok_in_ag = 0, tok_out_ag = 0;
            std::string ag_out = aggressiveShrink(out, tok_in_ag, tok_out_ag);
            int saved_pct = (tok_in_ag > 0)
                            ? (int)(100 - 100 * tok_out_ag / tok_in_ag)
                            : 0;
            std::cerr << "[icmg shrink: aggressive] "
                      << tok_in_ag << " tok (before) → "
                      << tok_out_ag << " tok (after, "
                      << saved_pct << "% saved)\n";
            out = std::move(ag_out);
        }

        std::cout << out;
        std::cerr << "[icmg shrink: " << label << "] "
                  << input.size() << "B → " << out.size() << "B ("
                  << (input.size() > 0 ? 100 - 100 * out.size() / input.size() : 0)
                  << "% off)\n";
        try {
            int tok_in  = (int)input.size() / 4;
            int tok_out = (int)out.size()   / 4;
            core::Db db(core::Config::instance().projectDbPath("."));
            compress::GlossaryStore store(db);
            store.recordTelemetry("shrink", (int)input.size(), (int)out.size(),
                                  tok_in, tok_out, 0, label);
        } catch (...) {}
        return 0;
    }
};

ICMG_REGISTER_COMMAND("shrink", ShrinkCommand);

} // namespace icmg::cli
