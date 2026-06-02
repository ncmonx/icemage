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
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../compress/compressor.hpp"
#include "../../compress/glossary_store.hpp"

#include <algorithm>
#include <fstream>
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
"  --kind <K>         Force kind (grep|build|sql|json|generic|compress)\n"
"  --threshold N      Skip if input < N bytes (default 8192)\n"
"  --json             Emit JSON metadata wrapper\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }

        // Read input.
        std::string input;
        std::string path;
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a == "--kind" || a == "--threshold") { ++i; continue; }
            if (!a.empty() && a[0] == '-') continue;
            path = a;
        }
        if (!path.empty()) {
            std::ifstream f(path, std::ios::binary);
            if (!f) { std::cerr << "icmg shrink: open " << path << " failed\n"; return 1; }
            std::ostringstream ss; ss << f.rdbuf();
            input = ss.str();
        } else {
            std::ostringstream ss; ss << std::cin.rdbuf();
            input = ss.str();
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
            // the byte budget (threshold). Pluggable score (heuristic infoScore now;
            // llama-logprob perplexity later). Opt-in via --kind salience.
            auto lines = splitLines(input);
            std::vector<double> scores; scores.reserve(lines.size());
            for (auto& ln : lines) scores.push_back(core::infoScore(ln));
            std::string out = core::selectByBudget(lines, scores, (size_t)threshold, "\n");
            std::cout << out << "\n";
            std::cerr << "[icmg shrink: salience] " << input.size() << "->" << out.size()
                      << " bytes (" << (input.size() > 0 ? 100 - 100 * out.size() / input.size() : 0)
                      << "% saved)\n";
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
