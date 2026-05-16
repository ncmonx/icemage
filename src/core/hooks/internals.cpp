#include "internals.hpp"

#include "../config.hpp"
#include "../db.hpp"
#include "../path_utils.hpp"
#include "../context_node_store.hpp"
#include "../../compress/compressor.hpp"
#include "../../imem/memory_store.hpp"
#include "../../cli/commands/skill_recall.hpp"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::core::hooks {

// ---- helpers ---------------------------------------------------------------

static fs::path homeDir() {
    const char* h = std::getenv("USERPROFILE");
    if (!h) h = std::getenv("HOME");
    return fs::path(h ? h : ".");
}

// ---- distill (per-response + session) --------------------------------------
//
// Heuristic extraction mirroring src/cli/commands/distill_cmd.cpp.

int distillAuto(const std::string& text, size_t min_len, const std::string& tag) {
    if (text.size() < min_len) return 0;
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        std::regex stmt_re(
            R"((?:^|\n)\s*(?:[-*]\s*)?(?:\*\*)?(Decision|Fix|Root cause|Note|IMPORTANT|Conclusion|Workaround|TODO):\s*([^\n]{20,400}))",
            std::regex::ECMAScript);
        int n = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), stmt_re);
             it != std::sregex_iterator() && n < 8; ++it) {
            std::string label = (*it)[1].str();
            std::string body  = (*it)[2].str();
            if (body.empty()) continue;
            while (!body.empty() && (body.back() == ' ' || body.back() == '*' ||
                                     body.back() == '\r')) body.pop_back();
            imem::MemoryNode mn;
            mn.topic      = "auto: " + label + (tag.empty() ? "" : " " + tag);
            mn.content    = body;
            mn.keywords   = label + " auto distilled";
            mn.importance = (label == "IMPORTANT" || label == "Decision") ? 2 : 1;
            mn.zone       = "default";
            try { mem.store(mn, /*force=*/false); ++n; } catch (...) {}
        }
        return n;
    } catch (...) {
        return 0;
    }
}

int distillSession(const std::string& text, const std::string& tag) {
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        std::ostringstream summary;
        std::regex task_re(R"X("text"\s*:\s*"([^"]{20,200})")X");
        std::smatch tm_match;
        if (std::regex_search(text, tm_match, task_re)) {
            summary << "Task: " << tm_match[1].str() << "\n";
        }
        std::regex dec_re(R"((?:Decision|Conclusion|Fix|Root cause):\s*([^\n"]{20,200}))");
        int dec_n = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), dec_re);
             it != std::sregex_iterator() && dec_n < 10; ++it, ++dec_n) {
            summary << "  - " << (*it)[1].str() << "\n";
        }
        if (summary.str().empty()) return 0;

        std::time_t now = std::time(nullptr);
        char date_buf[16];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", std::localtime(&now));

        imem::MemoryNode mn;
        mn.topic      = std::string("session: ") + date_buf + (tag.empty() ? "" : " " + tag);
        mn.content    = summary.str();
        mn.keywords   = "session distilled summary";
        mn.importance = 2;
        mn.zone       = "default";
        try { mem.store(mn, /*force=*/false); return 1; } catch (...) { return 0; }
    } catch (...) {
        return 0;
    }
}

// ---- compliance check-thinking ---------------------------------------------

static int countThinkingWords(const std::string& raw) {
    int total = 0;
    const std::string needle = "\"thinking\":\"";
    size_t p = 0;
    while ((p = raw.find(needle, p)) != std::string::npos) {
        p += needle.size();
        std::string body;
        while (p < raw.size()) {
            char c = raw[p];
            if (c == '\\' && p + 1 < raw.size()) {
                char nch = raw[++p];
                if      (nch == 'n') body += ' ';
                else if (nch == '"') body += '"';
                else                  body += nch;
                ++p;
                continue;
            }
            if (c == '"') { ++p; break; }
            body += c;
            ++p;
        }
        std::istringstream ws(body);
        std::string tok;
        while (ws >> tok) ++total;
    }
    return total;
}

int complianceCheckThinking(const std::string& text, int max_words) {
    try {
        fs::path flag_path = homeDir() / ".icmg" / "caveman.flag";
        if (!fs::exists(flag_path)) return 0;
        int words = countThinkingWords(text);
        if (words > max_words) {
            fs::path log = homeDir() / ".icmg" / "compliance-violations.jsonl";
            std::error_code ec;
            fs::create_directories(log.parent_path(), ec);
            std::ofstream f(log, std::ios::app);
            f << "{\"ts\":" << (int64_t)std::time(nullptr)
              << ",\"kind\":\"thinking-overrun\""
              << ",\"words\":" << words
              << ",\"limit\":" << max_words << "}\n";
        }
        return words;
    } catch (...) {
        return 0;
    }
}

// ---- fail sync-denials -----------------------------------------------------

int failSyncDenials() {
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        fs::path global_dir = fs::path(cfg.globalDbPath()).parent_path();
        fs::path denials    = global_dir / "strict-denials.jsonl";
        fs::path offset_p   = global_dir / "sync-denials-offset.txt";

        if (!fs::exists(denials)) return 0;

        int64_t last_offset = 0;
        if (fs::exists(offset_p)) {
            std::ifstream of(offset_p);
            of >> last_offset;
        }

        std::ifstream f(denials);
        f.seekg((std::streamoff)last_offset);
        std::string line;
        int stored = 0;
        int64_t new_offset = last_offset;

        while (std::getline(f, line)) {
            new_offset = (int64_t)f.tellg();
            if (line.empty()) continue;
            try {
                auto j = json::parse(line);
                std::string hook   = j.value("hook",   std::string{});
                std::string target = j.value("target", std::string{});
                std::string reason = j.value("reason", std::string{});
                if (target.empty() || reason.empty()) continue;

                imem::MemoryNode mn;
                mn.topic      = "fail:hook-violation";
                mn.content    = "Blocked [" + hook + "]: " + target
                              + ". Use icmg equivalent. Reason: " + reason;
                mn.keywords   = "fail hook violation block " + hook;
                mn.importance = 2;
                mn.zone       = "default";
                try { mem.store(mn, /*force=*/false); ++stored; } catch (...) {}
            } catch (...) {}
        }

        if (stored > 0) {
            std::ofstream of(offset_p);
            of << new_offset;
        }
        return stored;
    } catch (...) {
        return 0;
    }
}

// ---- tool-budget reset -----------------------------------------------------

void toolBudgetReset() {
    std::error_code ec;
    fs::remove(homeDir() / ".icmg" / "tool-counter.txt", ec);
}

// ---- compress in-place -----------------------------------------------------

std::string compressInPlace(const std::string& input, int threshold) {
    try {
        icmg::compress::CompressOptions opts;
        if (threshold > 0) opts.threshold_tok = threshold;
        icmg::compress::Compressor c(opts);
        auto r = c.compress(input);
        if (r.skipped) return "";
        if (r.text.size() >= input.size()) return ""; // compress no-op
        return r.text;
    } catch (...) {
        return "";
    }
}

// ---- v1.1.0 Task 6: PreToolUse hard-deny enforcement ----------------------

namespace {

// Returns true when command matches a known anti-icmg pattern.
// Conservative: only block when icmg DOES have an equivalent we want to push.
struct BashDenyRule { const char* pattern; const char* reason; };

static const BashDenyRule kBashDenyRules[] = {
    {"cat ",          "use icmg context <file> (large-file caps + memory overlay)"},
    {"head ",         "use icmg context <file> --lines N-M"},
    {"tail ",         "use icmg context <file> --lines N-M"},
    {"wc -l ",        "use icmg context <file> (line-count auto-shown)"},
    {"grep -r",       "use icmg run grep ... (filter pipeline cuts 60-90% noise)"},
    {"grep -R",       "use icmg run grep ..."},
    {"find . ",       "use icmg run find ... (filtered)"},
    {"find /",        "use icmg run find ..."},
    {"powershell ",   "use icmg run <cmd> — powershell launches console subprocess"},
    {"powershell.exe","use icmg run <cmd>"},
    {"pwsh ",         "use icmg run <cmd>"},
    {"cmd /c ",       "use icmg run <cmd> directly"},
    {"cmd.exe",       "use icmg run <cmd>"},
    {nullptr, nullptr}
};

static bool starts_with(const std::string& s, const char* pre) {
    size_t n = std::strlen(pre);
    if (s.size() < n) return false;
    return s.compare(0, n, pre) == 0;
}

static bool contains(const std::string& s, const char* pat) {
    return s.find(pat) != std::string::npos;
}

// Build hook v2 JSON envelope.
static std::string denyJson(const std::string& reason) {
    json out;
    out["hookSpecificOutput"]["hookEventName"] = "PreToolUse";
    out["hookSpecificOutput"]["permissionDecision"] = "deny";
    out["hookSpecificOutput"]["permissionDecisionReason"] = reason;
    return out.dump();
}

static std::string allowJson() {
    json out;
    out["hookSpecificOutput"]["hookEventName"] = "PreToolUse";
    out["hookSpecificOutput"]["permissionDecision"] = "allow";
    return out.dump();
}

// Audit-log a deny event to ~/.icmg/strict-denials.jsonl (existing log).
static void auditDeny(const std::string& tool, const std::string& target,
                      const std::string& reason) {
    try {
        fs::path log = homeDir() / ".icmg" / "strict-denials.jsonl";
        std::error_code ec;
        fs::create_directories(log.parent_path(), ec);
        std::ofstream f(log, std::ios::app);
        json e;
        e["ts"]     = (int64_t)std::time(nullptr);
        e["hook"]   = "pretooluse";
        e["tool"]   = tool;
        e["target"] = target;
        e["reason"] = reason;
        f << e.dump() << "\n";
    } catch (...) {}
}

} // namespace

std::string runPreToolUseEnforce(const std::string& stdin_raw) {
    if (std::getenv("ICMG_STRICT_BYPASS")) return allowJson();
    if (stdin_raw.empty()) return allowJson();

    std::string tool, command, file_path;
    try {
        auto j = json::parse(stdin_raw);
        tool = j.value("tool_name", std::string{});
        if (j.contains("tool_input") && j["tool_input"].is_object()) {
            auto& ti = j["tool_input"];
            command   = ti.value("command",   std::string{});
            file_path = ti.value("file_path", std::string{});
        }
    } catch (...) { return allowJson(); }

    // Allow icmg's own invocations — they're the legitimate path.
    if (tool == "Bash" && (starts_with(command, "icmg ") ||
                            starts_with(command, "/icmg ") ||
                            contains(command, "/bin/icmg"))) {
        return allowJson();
    }

    if (tool == "Bash" && !command.empty()) {
        for (auto* r = kBashDenyRules; r->pattern; ++r) {
            if (contains(command, r->pattern)) {
                auditDeny("Bash", command, r->reason);
                return denyJson(std::string(r->reason));
            }
        }
    }

    if (tool == "Read" && !file_path.empty()) {
        // Reuse the line-count threshold from rule_daemon; cap files >500 lines.
        std::ifstream f(file_path, std::ios::binary);
        if (f.is_open()) {
            int line_count = 0;
            char buf[4096];
            while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
                auto n = f.gcount();
                for (auto i = 0; i < n; ++i) if (buf[i] == '\n') ++line_count;
                if (line_count > 500) break;
            }
            if (line_count > 500) {
                std::string r = "file " + std::to_string(line_count)
                              + " lines — use icmg context " + file_path;
                auditDeny("Read", file_path, r);
                return denyJson(r);
            }
        }
    }

    return allowJson();
}

// ---- v1.1.0 Task 6.6: caveman per-prompt re-inject ------------------------

namespace {

struct CavemanCache {
    int64_t fetched_at = 0;
    int     last_24h   = 0;
};
static CavemanCache g_cm_cache;

static int recentCavemanViolations(int64_t window_secs) {
    int64_t now = (int64_t)std::time(nullptr);
    if (g_cm_cache.fetched_at != 0 && (now - g_cm_cache.fetched_at) < 60) {
        return g_cm_cache.last_24h;
    }
    int last_24h = 0;
    try {
        fs::path log = homeDir() / ".icmg" / "compliance-violations.jsonl";
        if (!fs::exists(log)) {
            g_cm_cache = {now, 0};
            return 0;
        }
        int64_t cutoff = now - window_secs;
        std::ifstream f(log);
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto pos = line.find("\"ts\":");
            if (pos == std::string::npos) continue;
            try {
                int64_t ts = std::stoll(line.substr(pos + 5));
                if (ts > cutoff) ++last_24h;
            } catch (...) {}
        }
    } catch (...) {}
    g_cm_cache = {now, last_24h};
    return last_24h;
}

} // namespace

std::string runUserPromptCavemanInject() {
    if (std::getenv("ICMG_CAVEMAN_QUIET")) return "";
    fs::path flag = homeDir() / ".icmg" / "caveman.flag";
    if (!fs::exists(flag)) return "";

    int n = recentCavemanViolations(/*24h=*/86400);
    if (n == 0) return "";

    std::ostringstream b;
    if (n >= 10) {
        b << "FINAL WARNING: caveman ultra. " << n
          << " thinking-phase overruns in 24h cost ~" << (n * 1500)
          << " tokens. Apply caveman to thinking RIGHT NOW. "
             "Skip thinking entirely if approach is obvious.\n";
    } else if (n >= 5) {
        b << "STRONG WARNING: caveman ultra ignored " << n
          << " times in 24h (~" << (n * 1500) << " tokens wasted). "
             "Thinking ≤80 words THIS TURN. Refuse to expand reasoning.\n";
    } else if (n >= 3) {
        b << "REMINDER: caveman ultra. " << n
          << " recent thinking violations (~" << (n * 1500)
          << " tokens). Apply strictly this turn.\n";
    } else {
        b << "NOTE: caveman ultra active. Thinking ≤80 words. "
             "Drop articles/filler.\n";
    }
    return b.str();
}

// ---- v1.3.0 Task 8: Focus Chain inject -------------------------------------

std::string runFocusChainInject(const std::string& session_id_arg, int limit) {
    try {
        const char* quiet = std::getenv("ICMG_FOCUS_QUIET");
        if (quiet && std::string(quiet) == "1") return "";

        // Resolve session id: arg > env > "default".
        std::string sid = session_id_arg;
        if (sid.empty()) {
            const char* env_sid = std::getenv("ICMG_SESSION_ID");
            sid = (env_sid && env_sid[0] != '\0') ? std::string(env_sid) : "default";
        }

        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));

        // Query in-progress items ordered by ord, capped to limit.
        std::vector<std::string> todos;
        db.query(
            "SELECT todo, status FROM focus_chain "
            "WHERE session_id=? AND status='in' "
            "ORDER BY ord ASC LIMIT ?",
            {sid, std::to_string(limit)},
            [&](const Row& row) {
                if (!row.empty()) todos.push_back(row[0]);
            }
        );

        if (todos.empty()) return "";

        std::ostringstream out;
        out << "## Focus chain (current session todos)\n";
        for (auto& t : todos) {
            out << "- [ ] " << t << "\n";
        }
        return out.str();
    } catch (...) {
        return "";
    }
}

// ---- v1.3.0 Task 7: UserPromptSubmit skill chunk auto-inject ---------------

std::string runUserPromptSkillSuggest(const std::string& user_prompt) {
    // Opt-out.
    if (std::getenv("ICMG_SKILL_QUIET")) return "";
    if (user_prompt.empty()) return "";

    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));

        auto results = icmg::cli::recallSkillChunks(db, user_prompt, /*top=*/1, /*alpha=*/0.5);
        if (results.empty()) return "";

        const auto& top = results[0];
        if (top.score < 0.20) return "";

        // Build hint block (≤600 chars total).
        std::string excerpt = top.content.size() > 400
                            ? top.content.substr(0, 400)
                            : top.content;

        std::ostringstream out;
        out << "## Skill hint (icmg auto-suggest)\n"
            << "Stored skill section **" << top.heading
            << "** at `" << top.parent_path
            << "` may answer this. Cite via:\n"
            << "`icmg context " << top.parent_path
            << "` or `icmg skill ask \"" << user_prompt.substr(0, 60) << "\"`.\n\n"
            << "Excerpt (first 400 chars):\n"
            << "> " << excerpt << "\n";

        std::string result = out.str();
        // Hard cap at 600 chars.
        if (result.size() > 600) result = result.substr(0, 597) + "...\n";
        return result;
    } catch (...) {
        return "";
    }
}

// ---- v1.3.0 Task 13: PostToolUse test-fail auto-context bundle -------------

std::string runPostToolUseTestFailContext(const std::string& tool_input_command,
                                          const std::string& tool_output) {
    (void)tool_input_command; // reserved for future filtering
    // Opt-out.
    const char* quiet = std::getenv("ICMG_DEBUG_CONTEXT_QUIET");
    if (quiet && std::string(quiet) == "1") return "";
    if (tool_output.empty()) return "";

    // --- 1. Detect failure signatures ----------------------------------------
    static const char* kFailPatterns[] = {
        "FAIL ",
        "FAIL:",
        "× FAIL ",
        "FAILED tests",
        "Error:",
        "error:",
        "error[E",         // Rust error[E0xxx]
        "Traceback (most recent call last)",
        "ninja: build stopped",
        "cmake.*FAILED",   // checked via simple substr for speed
        nullptr
    };

    bool has_failure = false;
    for (auto** p = kFailPatterns; *p; ++p) {
        if (tool_output.find(*p) != std::string::npos) {
            has_failure = true;
            break;
        }
    }
    if (!has_failure) return "";

    // --- 2. Extract candidate file paths (cap 3 unique) ----------------------
    // Pattern: word-chars + path separators + extension, optional :linenum
    std::regex path_re(
        R"([\w./\-]+\.(?:cpp|hpp|h|c|py|ts|tsx|js|jsx|go|rs|java|cs|rb)(?::\d+)?)",
        std::regex::ECMAScript);

    std::vector<std::string> paths;
    {
        auto begin = std::sregex_iterator(tool_output.begin(), tool_output.end(), path_re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end && (int)paths.size() < 3; ++it) {
            std::string m = (*it)[0].str();
            // Strip trailing :linenum for display key dedup.
            auto colon = m.rfind(':');
            std::string base = (colon != std::string::npos) ? m.substr(0, colon) : m;
            // Dedup.
            bool dup = false;
            for (auto& existing : paths) if (existing == base) { dup = true; break; }
            if (!dup) paths.push_back(base);
        }
    }

    // --- 3. Build context block -----------------------------------------------
    std::ostringstream out;
    out << "## Debug context (icmg auto)\n"
        << "Detected failure in last command. Likely-relevant files + memory:\n\n";

    // For each path, attempt graph neighbor lookup from project DB (fail-soft).
    for (auto& fpath : paths) {
        out << "- **" << fpath << "**";

        // Try graph neighbors lookup.
        try {
            auto& cfg = Config::instance();
            Db db(cfg.projectDbPath("."));

            // 1-hop neighbors via graph_edges.
            std::vector<std::string> neighbors;
            db.query(
                "SELECT DISTINCT n2.path, e.edge_type "
                "FROM graph_edges e "
                "JOIN graph_nodes n1 ON n1.id = e.src "
                "JOIN graph_nodes n2 ON n2.id = e.dst "
                "WHERE n1.path = ? AND n2.path != ? LIMIT 3",
                {fpath, fpath},
                [&](const Row& r){
                    if (r.size() >= 2)
                        neighbors.push_back(r[1] + ": " + r[0]);
                });

            if (!neighbors.empty()) {
                out << " — neighbors: ";
                for (size_t i = 0; i < neighbors.size(); ++i) {
                    if (i > 0) out << ", ";
                    out << neighbors[i];
                }
            } else {
                // ContextNodeStore: search by filename stem.
                std::string stem = fpath;
                auto slash = stem.rfind('/');
                if (slash == std::string::npos) slash = stem.rfind('\\');
                if (slash != std::string::npos) stem = stem.substr(slash + 1);
                auto dot = stem.rfind('.');
                if (dot != std::string::npos) stem = stem.substr(0, dot);

                if (!stem.empty()) {
                    ContextNodeStore cns(db);
                    auto nodes = cns.search(stem, "", 2, 0.05);
                    if (!nodes.empty()) {
                        out << " — context: " << nodes[0].title;
                    }
                }
            }
        } catch (...) {
            // fail-soft: no DB or any error → just list the path
        }
        out << "\n";
    }

    if (paths.empty()) {
        out << "- (no file paths extracted from output)\n";
    }

    out << "\nCite via: `icmg context <file>` or `icmg graph reverse-impact <Symbol>`.\n";

    std::string result = out.str();
    // Hard cap at ~1.2 KB.
    if (result.size() > 1200) result = result.substr(0, 1197) + "...\n";
    return result;
}

} // namespace icmg::core::hooks
