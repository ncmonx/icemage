#include "internals.hpp"

#include "../config.hpp"
#include "../db.hpp"
#include "../path_utils.hpp"
#include "../context_node_store.hpp"
#include "../target_disambiguator.hpp"
#include "../rule_telemetry.hpp"  // v1.35.0 R8
#include "../global_db.hpp"  // v1.38.0 A7 amnesia_events
#include "../../compress/compressor.hpp"
#include "../../imem/memory_store.hpp"
#include "../../cli/commands/skill_recall.hpp"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
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

// v1.21.4 (X1): PreCompact per-snippet preservation.
//
// distillAuto caps at 8 matches and is geared for per-response calls.
// PreCompact fires once over an entire session transcript — capture more
// (cap 30) and use a slightly broader pattern that also picks up "prefer",
// "always", "never" preference statements. Each match becomes its own node
// so future BM25/semantic recall can hit them individually.
//
// Opt-out: ICMG_NO_X1_EXTRACT=1.
int extractPreCompactSnippets(const std::string& text) {
    if (std::getenv("ICMG_NO_X1_EXTRACT")) return 0;
    if (text.size() < 200) return 0;
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        std::regex stmt_re(
            R"((?:^|\n|\\n)\s*(?:[-*]\s*)?(?:\*\*)?(Decision|Fix|Root cause|Note|IMPORTANT|Conclusion|Workaround|TODO|Prefer|Always|Never|Known issue):\s*([^\n"]{20,400}))",
            std::regex::ECMAScript | std::regex::icase);

        std::time_t now = std::time(nullptr);
        char date_buf[16];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", std::localtime(&now));

        int n = 0;
        std::set<std::string> seen;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), stmt_re);
             it != std::sregex_iterator() && n < 30; ++it) {
            std::string label = (*it)[1].str();
            std::string body  = (*it)[2].str();
            while (!body.empty() && (body.back() == ' ' || body.back() == '*' ||
                                     body.back() == '\r' || body.back() == '\\'))
                body.pop_back();
            if (body.size() < 20) continue;
            // De-duplicate within this transcript pass (the same statement
            // often repeats in tool-call echoes).
            std::string key = label + "|" + body.substr(0, 80);
            if (!seen.insert(key).second) continue;

            imem::MemoryNode mn;
            mn.topic      = std::string("auto:precompact-") + date_buf
                          + "-" + std::to_string(n);
            mn.content    = label + ": " + body;
            mn.keywords   = label + " precompact auto x1";
            mn.importance = (label == "IMPORTANT" || label == "Decision"
                          || label == "Root cause") ? 2 : 1;
            mn.zone       = "default";
            try { mem.store(mn, /*force=*/false); ++n; } catch (...) {}
        }
        return n;
    } catch (...) {
        return 0;
    }
}

// v1.21.7 (FB2): persist raw transcript into FTS5-indexed table for later
// full-text search. Called from PreCompact runner before snippet-extract.
int recordTranscript(const std::string& session_id,
                     const std::string& text,
                     size_t max_chars) {
    if (std::getenv("ICMG_NO_TRANSCRIPT_STORE")) return 0;
    if (text.empty()) return 0;
    try {
        std::string content = text;
        if (content.size() > max_chars) {
            content.resize(max_chars);
            content += "\n... (truncated at " + std::to_string(max_chars)
                     + " chars by recordTranscript)\n";
        }
        std::string sid = session_id.empty() ? std::string("unknown") : session_id;
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        db.run("INSERT INTO transcripts(session_id, content, char_len) "
               "VALUES(?,?,?)",
               {sid, content, std::to_string((int)content.size())});
        return 1;
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
        fs::path flag_path = homeDir() / ".icmg" / "sayless.flag";
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
    {"powershell ",   "use icmg run <cmd> â€” powershell launches console subprocess"},
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

    // Allow icmg's own invocations â€” they're the legitimate path.
    auto is_icmg_invocation = [&](){
        return starts_with(command, "icmg ") ||
               starts_with(command, "/icmg ") ||
               contains(command, "/bin/icmg") ||
               contains(command, "icmg.exe");
    };

    if (tool == "Bash" && is_icmg_invocation()) return allowJson();

    if (tool == "Bash" && !command.empty()) {
        for (auto* r = kBashDenyRules; r->pattern; ++r) {
            if (contains(command, r->pattern)) {
                auditDeny("Bash", command, r->reason);
                return denyJson(std::string(r->reason)
                    + "\n\nSubstitution table:\n"
                      "  Read/cat        -> icmg context <file> [--lines A-B]\n"
                      "  Grep/Glob       -> icmg graph search <kw> | icmg run grep ...\n"
                      "  ls/find         -> icmg ls [path] | icmg run find ...\n"
                      "  Bash git        -> icmg run git ... (filtered output)\n"
                      "Bypass: ICMG_STRICT_BYPASS=1");
            }
        }
    }

    // v1.6.3: PowerShell bypass guard.
    if (tool == "PowerShell" && !command.empty()) {
        if (is_icmg_invocation()) return allowJson();
        std::string reason =
            "PowerShell bypasses icmg-first rule. Use:\n"
            "  icmg context <file>     (replace Read/cat/Get-Content)\n"
            "  icmg graph search <kw>  (replace Grep/Select-String)\n"
            "  icmg run <cmd>          (replace generic shell)\n"
            "  icmg ls [path]          (replace Get-ChildItem)\n"
            "  icmg run git ...        (replace direct git invocation)\n"
            "Bypass per-session: ICMG_STRICT_BYPASS=1";
        auditDeny("PowerShell", command, "PowerShell bypass blocked");
        return denyJson(reason);
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
                              + " lines â€” use icmg context " + file_path;
                auditDeny("Read", file_path, r);
                return denyJson(r);
            }
        }
    }

    return allowJson();
}

// ---- v1.1.0 Task 6.6: sayless per-prompt re-inject ------------------------

namespace {

struct SaylessCache {
    int64_t fetched_at = 0;
    int     last_24h   = 0;
};
static SaylessCache g_cm_cache;

static int recentSaylessViolations(int64_t window_secs) {
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

std::string runUserPromptSaylessInject() {
    if (std::getenv("ICMG_SAYLESS_QUIET")) return "";
    fs::path flag = homeDir() / ".icmg" / "sayless.flag";
    if (!fs::exists(flag)) return "";

    int n = recentSaylessViolations(/*24h=*/86400);
    if (n == 0) return "";

    std::ostringstream b;
    if (n >= 10) {
        b << "FINAL WARNING: sayless ultra. " << n
          << " thinking-phase overruns in 24h cost ~" << (n * 1500)
          << " tokens. Apply sayless to thinking RIGHT NOW. "
             "Skip thinking entirely if approach is obvious.\n";
    } else if (n >= 5) {
        b << "STRONG WARNING: sayless ultra ignored " << n
          << " times in 24h (~" << (n * 1500) << " tokens wasted). "
             "Thinking â‰¤80 words THIS TURN. Refuse to expand reasoning.\n";
    } else if (n >= 3) {
        b << "REMINDER: sayless ultra. " << n
          << " recent thinking violations (~" << (n * 1500)
          << " tokens). Apply strictly this turn.\n";
    } else {
        b << "NOTE: sayless ultra active. Thinking â‰¤80 words. "
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

        // Build hint block (â‰¤600 chars total).
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
        "Ã— FAIL ",
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
                out << " â€” neighbors: ";
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
                        out << " â€” context: " << nodes[0].title;
                    }
                }
            }
        } catch (...) {
            // fail-soft: no DB or any error â†’ just list the path
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

// ---- v1.4.0 Task 1: PreToolUse:Edit disambiguation hook -------------------

std::string runPreToolUseEditDisambig(const std::string& stdin_raw) {
    // Opt-out.
    if (std::getenv("ICMG_DISAMBIG_QUIET")) return "";
    if (stdin_raw.empty()) return "";

    // Parse threshold from env (default 0.80).
    double threshold = 0.80;
    if (const char* e = std::getenv("ICMG_DISAMBIG_THRESHOLD")) {
        try { threshold = std::stod(e); } catch (...) {}
    }

    // Parse tool name and file_path from stdin.
    std::string tool_name, file_path;
    try {
        auto j = json::parse(stdin_raw);
        tool_name = j.value("tool_name", std::string{});
        if (j.contains("tool_input") && j["tool_input"].is_object()) {
            file_path = j["tool_input"].value("file_path", std::string{});
        }
    } catch (...) { return ""; }

    // Only applies to Edit / Write tool calls.
    if (tool_name != "Edit" && tool_name != "Write") return "";
    if (file_path.empty()) return "";

    // Extract basename for matching.
    std::string basename = fs::path(file_path).filename().string();
    if (basename.empty()) return "";

    // Gather candidates from context node store. Fail-soft.
    std::vector<std::pair<std::string,std::string>> raw_candidates;
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        ContextNodeStore store(db);
        auto nodes = store.list("", true);
        for (auto& n : nodes) {
            raw_candidates.push_back({n.node_key, n.title});
        }
    } catch (...) {
        return ""; // fail-soft: no DB
    }

    if (raw_candidates.empty()) return "";

    // Use the basename as the "prompt" to score against all candidates.
    auto results = icmg::core::disambiguateTargets(basename, raw_candidates, threshold);

    // Only flag when >=2 candidates above threshold.
    if (results.size() < 2) return "";

    // Check if the target file_path is clearly the top match.
    if (!results.empty()) {
        std::string top_name = results[0].name;
        if (top_name == basename && results[0].score > threshold + 0.10) return "";
    }

    // Build candidates description.
    std::ostringstream reason;
    reason << "Ambiguous target: '" << basename << "' matches "
           << results.size() << " context nodes";
    std::string sep = " (";
    int shown = 0;
    for (auto& r : results) {
        if (shown >= 3) break;
        reason << sep << r.name;
        if (!r.path.empty() && r.path != r.name)
            reason << " at " << r.path;
        sep = ", ";
        ++shown;
    }
    reason << "). Confirm correct target before editing.";

    json hook_out;
    hook_out["hookSpecificOutput"]["hookEventName"]            = "PreToolUse";
    hook_out["hookSpecificOutput"]["permissionDecision"]       = "ask";
    hook_out["hookSpecificOutput"]["permissionDecisionReason"] = reason.str();
    return hook_out.dump();
}


// ---- v1.4.0 Task 2: PreToolUse:Bash git guard ------------------------------
//
// Detects: git checkout <file-path>, git restore <file-path>,
//          git reset --hard <file-path>
// Does NOT block: git checkout <branch>, git reset --hard <commit-hash>

std::string runPreToolUseBashGitGuard(const std::string& stdin_raw) {
    // Opt-out env.
    if (std::getenv("ICMG_GIT_GUARD_QUIET")) return allowJson();
    if (stdin_raw.empty()) return allowJson();

    std::string tool, command;
    try {
        auto j = json::parse(stdin_raw);
        tool = j.value("tool_name", std::string{});
        if (j.contains("tool_input") && j["tool_input"].is_object()) {
            command = j["tool_input"].value("command", std::string{});
        }
    } catch (...) { return allowJson(); }

    if (tool != "Bash" || command.empty()) return allowJson();

    // Allow icmg's own invocations.
    if (starts_with(command, "icmg ") ||
        starts_with(command, "/icmg ") ||
        contains(command, "/bin/icmg")) {
        return allowJson();
    }

    // --- Pattern matching ---
    // File-path discriminator: paths contain '.' or '/' or '\'.
    // Blocks: git checkout -- <file>, git checkout <path/file>, git checkout <file.ext>
    // Blocks: git restore <file>
    // Blocks: git reset --hard <path/file> or <file.ext> (not commit hashes or HEAD)
    // Allows: git checkout main, git checkout HEAD, git reset --hard HEAD~2
    static const std::regex re_checkout(
        R"(git\s+checkout\s+(?:[^\n]*?\s+)?(--\s+\S+|\S*[./\\]\S*))",
        std::regex::ECMAScript);
    static const std::regex re_restore(
        R"(git\s+restore\s+(?:--\S+\s+)*(\S+))",
        std::regex::ECMAScript);
    static const std::regex re_reset_hard(
        R"(git\s+reset\s+--hard\s+(\S+))",
        std::regex::ECMAScript);

    bool matched = false;
    std::string file_hint;

    std::smatch m;
    if (std::regex_search(command, m, re_checkout)) {
        std::string tok = m[1].str();
        // Must look like a file path: contains dot, slash, or starts with "--"
        if (tok.find('.') != std::string::npos ||
            tok.find('/') != std::string::npos ||
            tok.find('\\') != std::string::npos ||
            (tok.size() > 3 && tok.substr(0,3) == "-- ")) {
            matched = true;
            // Strip "-- " prefix if present
            file_hint = (tok.size() > 3 && tok.substr(0,3) == "-- ")
                        ? tok.substr(3) : tok;
        }
    } else if (std::regex_search(command, m, re_restore)) {
        std::string tok = m[1].str();
        // git restore always operates on files — any non-flag arg is a path
        if (!tok.empty() && tok[0] != '-') {
            matched = true;
            file_hint = tok;
        }
    } else if (std::regex_search(command, m, re_reset_hard)) {
        std::string tok = m[1].str();
        // Block only if the token looks like a file path (contains . or /)
        // Not HEAD, HEAD~N, or a 40-char hex hash
        bool is_head = (tok == "HEAD" || tok.substr(0, 5) == "HEAD~" || tok.substr(0, 5) == "HEAD^");
        bool is_hash = (tok.size() >= 7 && tok.size() <= 40 &&
                        tok.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos);
        bool is_path = (tok.find('.') != std::string::npos ||
                        tok.find('/') != std::string::npos ||
                        tok.find('\\') != std::string::npos);
        if (!is_head && !is_hash && is_path) {
            matched = true;
            file_hint = tok;
        }
    }

    if (!matched) return allowJson();

    std::string reason =
        "Use `icmg safe-rollback " + file_hint +
        "` instead — protects uncommitted work by showing diff and backing up "
        "to ~/.icmg/rollback-backups/ before checkout. "
        "Bypass: set ICMG_GIT_GUARD_QUIET=1.";

    auditDeny("Bash", command, reason);
    return denyJson(reason);
}


// ---- v1.4.0 Task 3: PostToolUse:Edit/Write auto graph-update + memory draft -

std::string runPostToolUseEditAutoSync(const std::string& stdin_raw) {
    // Opt-out env.
    if (std::getenv("ICMG_AUTO_SYNC_QUIET")) return "";
    if (stdin_raw.empty()) return "";

    // Parse tool name, file_path, and change content from stdin.
    std::string tool_name, file_path, old_string, new_string, content;
    try {
        auto j = json::parse(stdin_raw);
        tool_name = j.value("tool_name", std::string{});
        if (j.contains("tool_input") && j["tool_input"].is_object()) {
            auto& ti = j["tool_input"];
            file_path  = ti.value("file_path",  std::string{});
            old_string = ti.value("old_string", std::string{});
            new_string = ti.value("new_string", std::string{});
            content    = ti.value("content",    std::string{});
        }
    } catch (...) {
        return ""; // fail-soft: malformed JSON
    }

    // Only handle Edit / Write tool calls.
    if (tool_name != "Edit" && tool_name != "Write") return "";
    if (file_path.empty()) return "";

    // Resolve project root (cwd at hook invocation time).
    fs::path proj_root;
    try {
        proj_root = fs::current_path();
    } catch (...) {
        return "";
    }

    // Skip files outside the project root (absolute path check).
    fs::path fp(file_path);
    try {
        fp = fs::absolute(fp);
        std::string fp_str = fp.string();
        std::string pr_str = fs::absolute(proj_root).string();
        if (fp_str.size() < pr_str.size() ||
            fp_str.compare(0, pr_str.size(), pr_str) != 0) {
            return ""; // outside project root -- skip
        }
    } catch (...) {
        // If we can't determine, proceed conservatively (don't skip).
    }

    // Compute relative path for the topic key.
    std::string rel_path = file_path;
    try {
        fs::path rel = fs::relative(fp, proj_root);
        rel_path = rel.string();
        // Normalise separators to forward slash.
        for (auto& c : rel_path) if (c == '\\') c = '/';
    } catch (...) {}

    // TODO: in-process graph update -- no in-process GraphStore::scan() variant
    // exposed at hook level; avoid subprocess spawn per task constraint.
    // Deferred: call GraphStore::resolveAndInsertEdges() when a non-blocking
    // in-process API is added. For now, the graph update is a no-op here.

    // Compose draft memory entry.
    std::string draft_content;
    if (tool_name == "Write") {
        // For Write: first 200 chars of new content.
        std::string snip = content.substr(0, std::min(content.size(), size_t(200)));
        draft_content = "Write to " + rel_path + ": " + snip;
    } else {
        // For Edit: diff summary.
        draft_content = "Edit at " + rel_path +
                        ": -" + std::to_string(old_string.size()) + "b" +
                        " +" + std::to_string(new_string.size()) + "b";
    }

    // Insert draft ContextNode (fail-soft on DB unavailable).
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        ContextNodeStore store(db);

        ContextNode node;
        node.node_key    = "auto-draft:" + rel_path;
        node.title       = "auto-draft: " + rel_path;
        node.content     = draft_content;
        node.source_file = file_path;
        node.tier        = "draft";
        node.tags        = "[\"auto-draft\",\"edit-sync\"]";
        node.active      = true;

        store.upsert(node);
    } catch (...) {
        // fail-soft: DB open failure, schema mismatch, etc.
    }

    // v1.6.3: append touched file to pending graph-scan queue. Stop
    // hook fires `icmg graph scan` once per turn (deferred) so the
    // graph never lags the latest Edit/Write, but heavy scan does not
    // block the live PostToolUse path.
    try {
        fs::path pending = homeDir() / ".icmg" / "pending-graph-scan.list";
        std::error_code ec;
        fs::create_directories(pending.parent_path(), ec);
        // Read existing entries to dedupe (small set, expected <100).
        std::set<std::string> seen;
        {
            std::ifstream rf(pending);
            std::string line;
            while (std::getline(rf, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) seen.insert(line);
            }
        }
        if (seen.insert(fp.string()).second) {
            std::ofstream af(pending, std::ios::app);
            if (af) af << fp.string() << "\n";
        }
    } catch (...) {}

    // Always return "" -- this is a silent side-effect hook.
    return "";
}


// ---- v1.4.0 Task 5: PreToolUse approach-history inject --------------------

std::string runUserPromptApproachInject(const std::string& user_prompt) {
    if (user_prompt.empty()) return "";
    if (std::getenv("ICMG_APPROACH_QUIET")) return "";

    std::unique_ptr<core::Db> db;
    try {
        auto& cfg = core::Config::instance();
        db = std::make_unique<core::Db>(cfg.projectDbPath("."));
    } catch (...) { return ""; }

    // Tokenize prompt (lowercase, alnum tokens, drop <=2 chars).
    std::vector<std::string> tokens;
    {
        std::string cur;
        for (char c : user_prompt) {
            if (std::isalnum((unsigned char)c)) cur += (char)std::tolower((unsigned char)c);
            else { if (cur.size() > 2) tokens.push_back(cur); cur.clear(); }
        }
        if (cur.size() > 2) tokens.push_back(cur);
    }
    if (tokens.empty()) return "";

    struct Row { std::string task, approach, outcome, why; };
    std::map<std::string, std::vector<Row>> task_groups;
    for (auto& tok : tokens) {
        std::string pat = "%" + tok + "%";
        try {
            db->query(
                "SELECT task, approach, outcome, COALESCE(why,'') FROM approaches "
                "WHERE task LIKE ? COLLATE NOCASE "
                "ORDER BY CASE outcome WHEN 'success' THEN 0 WHEN 'partial' THEN 1 ELSE 2 END, "
                "created_at DESC LIMIT 3",
                {pat},
                [&](const core::Row& r) {
                    if (r.size() < 4) return;
                    Row row{r[0], r[1], r[2], r[3]};
                    task_groups[row.task].push_back(row);
                });
        } catch (...) { /* fail-soft per token */ }
    }
    if (task_groups.empty()) return "";

    std::ostringstream out;
    out << "## Past attempts (icmg approach history)\n";
    for (auto& kv : task_groups) {
        const auto& tname = kv.first;
        const auto& rows = kv.second;
        std::string display_task = tname.size() > 60 ? tname.substr(0, 57) + "..." : tname;
        out << "For task matching \"" << display_task << "\":\n";
        for (auto& r : rows) {
            std::string icon = (r.outcome == "success") ? "OK" :
                               (r.outcome == "partial") ? "PARTIAL" : "FAIL";
            std::string ap = r.approach.size() > 80 ? r.approach.substr(0, 77) + "..." : r.approach;
            out << "- [" << icon << "] " << ap << " (" << r.outcome << ")";
            if (!r.why.empty()) {
                std::string why = r.why.size() > 80 ? r.why.substr(0, 77) + "..." : r.why;
                out << " -- " << why;
            }
            out << "\n";
        }
    }
    out << "Prefer success path; cite via `icmg approach lookup \"<task>\"`\n";

    std::string result = out.str();
    if (result.size() > 600) result = result.substr(0, 597) + "...\n";
    return result;
}

// ---- v1.4.0 Task 5: PostToolUse:Bash test-outcome auto-record -------------

std::string runPostToolUseTestOutcome(const std::string& tool_input_command,
                                      const std::string& tool_output,
                                      int exit_code) {
    (void)exit_code;
    if (std::getenv("ICMG_APPROACH_QUIET")) return "";

    static const char* kTestRunners[] = {
        "ctest", "npm test", "pytest", "cargo test", "go test", nullptr
    };
    bool is_test_cmd = false;
    for (auto** r = kTestRunners; *r; ++r) {
        if (tool_input_command.find(*r) != std::string::npos) {
            is_test_cmd = true;
            break;
        }
    }
    if (!is_test_cmd) return "";

    static const char* kSuccessPatterns[] = {
        "100% tests passed", "0 failed", "All tests passed", "PASSED",
        "test result: ok", nullptr
    };
    static const char* kFailPatterns[] = {
        "tests failed", "FAILED", "Traceback", nullptr
    };

    std::string outcome;
    for (auto** p = kSuccessPatterns; *p; ++p) {
        if (tool_output.find(*p) != std::string::npos) { outcome = "success"; break; }
    }
    if (outcome.empty()) {
        for (auto** p = kFailPatterns; *p; ++p) {
            if (tool_output.find(*p) != std::string::npos) { outcome = "fail"; break; }
        }
    }
    if (outcome.empty()) return "";

    std::string focus_todo;
    try {
        std::string sid;
        const char* env_sid = std::getenv("ICMG_SESSION_ID");
        sid = (env_sid && env_sid[0] != '\0') ? std::string(env_sid) : "default";

        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));

        db.query(
            "SELECT todo FROM focus_chain "
            "WHERE session_id=? AND status='in' "
            "ORDER BY ord ASC LIMIT 1",
            {sid},
            [&](const Row& row) {
                if (!row.empty()) focus_todo = row[0];
            }
        );
    } catch (...) {
        return "";
    }

    if (focus_todo.empty()) return "";

    std::string approach_str = tool_input_command.size() > 200
                             ? tool_input_command.substr(0, 200)
                             : tool_input_command;
    std::string why_str = tool_output.size() > 200
                        ? tool_output.substr(0, 200)
                        : tool_output;

    std::string session_id;
    const char* env_sid2 = std::getenv("ICMG_SESSION_ID");
    if (env_sid2 && env_sid2[0] != '\0') session_id = std::string(env_sid2);

    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        db.run(
            "INSERT INTO approaches (task, approach, outcome, why, session_id) "
            "VALUES (?, ?, ?, ?, ?)",
            {focus_todo, approach_str, outcome, why_str, session_id}
        );
    } catch (...) {
        // fail-soft
    }

    return "";
}


// v1.33.0 R6: pinned rules auto-inject. Read top-N active rules from
// project rule store, format compact for additionalContext header.
std::string runUserPromptPinnedRulesInject(int max_rules) {
    if (std::getenv("ICMG_RULE_INJECT_QUIET")) return "";
    if (max_rules <= 0) max_rules = 5;
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        std::ostringstream out;
        int n = 0;
        const std::string sql =
            "SELECT name, content FROM rules "
            "WHERE active = 1 AND rule_type IN ('workflow','coding','arch') "
            "ORDER BY priority DESC, id ASC LIMIT " + std::to_string(max_rules);
        db.query(sql, {}, [&](const Row& r){
            if (r.size() < 2) return;
            if (n == 0) out << "ACTIVE RULES (icmg rule list):\n";
            std::string c = r[1];
            if (c.size() > 100) c = c.substr(0, 97) + "...";
            out << "  - " << r[0] << ": " << c << "\n";
            ++n;
        });
        if (n > 0) out << "---\n";
        return out.str();
    } catch (...) { return ""; }
}

// v1.33.0 R7: sibling projects auto-inject. Show top-3 by recent activity
// from global registry — prevents AI from forgetting user has multiple
// active codebases.
std::string runUserPromptProjectsInject() {
    if (std::getenv("ICMG_PROJECTS_INJECT_QUIET")) return "";
    try {
        const char* home =
#ifdef _WIN32
            std::getenv("USERPROFILE");
#else
            std::getenv("HOME");
#endif
        if (!home || !*home) return "";
        fs::path gpath = fs::path(home) / ".icmg" / "global.db";
        if (!fs::exists(gpath)) return "";
        Db gdb(gpath.string());
        // Identify current project by cwd to exclude it.
        std::string cwd = fs::current_path().string();
        for (char& c : cwd) if (c == '\\') c = '/';
        std::ostringstream out;
        int n = 0;
        gdb.query(
            "SELECT name, path, updated_at FROM projects "
            "ORDER BY updated_at DESC LIMIT 6",
            {}, [&](const Row& r){
                if (n >= 3 || r.size() < 2) return;
                std::string p = r[1];
                for (char& c : p) if (c == '\\') c = '/';
                if (p == cwd) return; // skip current
                if (n == 0) out << "OTHER ACTIVE PROJECTS (icmg project list):\n";
                out << "  - " << r[0] << " (" << p << ")\n";
                ++n;
            });
        if (n > 0) out << "---\n";
        return out.str();
    } catch (...) { return ""; }
}

// v1.34.0 A1: known-issue auto-recall. Top-2 errors-resolved% nodes
// matching prompt keyword. ≤180 chars total.
std::string runUserPromptKnownIssueInject(const std::string& user_prompt) {
    if (std::getenv("ICMG_KNOWN_ISSUE_QUIET")) return "";
    if (user_prompt.size() < 4) return "";
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        // Extract 3 longest alpha tokens from prompt as keyword pool.
        std::vector<std::string> toks;
        {
            std::string cur;
            for (char c : user_prompt) {
                if (std::isalnum(static_cast<unsigned char>(c))) cur.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                else { if (cur.size() >= 4) toks.push_back(cur); cur.clear(); }
            }
            if (cur.size() >= 4) toks.push_back(cur);
        }
        if (toks.empty()) return "";
        std::sort(toks.begin(), toks.end(), [](const auto& a, const auto& b){ return a.size() > b.size(); });
        if (toks.size() > 3) toks.resize(3);
        std::string like_or;
        std::vector<std::string> binds;
        for (auto& t : toks) {
            if (!like_or.empty()) like_or += " OR ";
            like_or += "(topic LIKE ? OR content LIKE ?)";
            binds.push_back("%" + t + "%");
            binds.push_back("%" + t + "%");
        }
        std::ostringstream out;
        int n = 0;
        db.query(
            "SELECT topic, content FROM memory_nodes "
            "WHERE deleted_at IS NULL AND topic LIKE 'errors-resolved%' "
            "AND (" + like_or + ") "
            "ORDER BY last_used DESC LIMIT 2",
            binds, [&](const Row& r){
                if (r.size() < 2) return;
                if (n == 0) out << "KNOWN ISSUES (matching this task):\n";
                std::string c = r[1];
                if (c.size() > 90) c = c.substr(0, 87) + "...";
                out << "  - " << r[0] << ": " << c << "\n";
                ++n;
            });
        if (n > 0) out << "---\n";
        return out.str();
    } catch (...) { return ""; }
}

// v1.34.0 A2: fail auto-recall. Top-2 fail:* anti-pattern nodes matching
// prompt keywords. Helps AI avoid repeating known-bad approaches.
std::string runUserPromptFailInject(const std::string& user_prompt) {
    if (std::getenv("ICMG_FAIL_INJECT_QUIET")) return "";
    if (user_prompt.size() < 4) return "";
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        std::vector<std::string> toks;
        {
            std::string cur;
            for (char c : user_prompt) {
                if (std::isalnum(static_cast<unsigned char>(c))) cur.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                else { if (cur.size() >= 4) toks.push_back(cur); cur.clear(); }
            }
            if (cur.size() >= 4) toks.push_back(cur);
        }
        if (toks.empty()) return "";
        std::sort(toks.begin(), toks.end(), [](const auto& a, const auto& b){ return a.size() > b.size(); });
        if (toks.size() > 3) toks.resize(3);
        std::string like_or;
        std::vector<std::string> binds;
        for (auto& t : toks) {
            if (!like_or.empty()) like_or += " OR ";
            like_or += "(topic LIKE ? OR content LIKE ?)";
            binds.push_back("%" + t + "%");
            binds.push_back("%" + t + "%");
        }
        std::ostringstream out;
        int n = 0;
        db.query(
            "SELECT topic, content FROM memory_nodes "
            "WHERE deleted_at IS NULL AND topic LIKE 'fail:%' "
            "AND (" + like_or + ") "
            "ORDER BY last_used DESC LIMIT 2",
            binds, [&](const Row& r){
                if (r.size() < 2) return;
                if (n == 0) out << "AVOID — PAST FAILURES:\n";
                std::string c = r[1];
                if (c.size() > 90) c = c.substr(0, 87) + "...";
                out << "  - " << r[0] << ": " << c << "\n";
                ++n;
            });
        if (n > 0) out << "---\n";
        return out.str();
    } catch (...) { return ""; }
}

// v1.34.0 A3: recent decisions inject. Last 3 [saved] entries from
// session-log.md tail. ≤300 chars compact form.
std::string runUserPromptRecentDecisionsInject() {
    if (std::getenv("ICMG_DECISIONS_INJECT_QUIET")) return "";
    try {
        fs::path log = fs::current_path() / "session-log.md";
        if (!fs::exists(log)) return "";
        std::ifstream f(log);
        if (!f) return "";
        // Read last ~30 KB tail for speed.
        f.seekg(0, std::ios::end);
        std::streamoff sz = f.tellg();
        std::streamoff start = sz > 30000 ? sz - 30000 : 0;
        f.seekg(start);
        std::string buf;
        buf.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        // Find last 3 "## YYYY-MM-DD HH:MM [saved]" headers.
        std::regex header(R"(## (\d{4}-\d{2}-\d{2} \d{2}:\d{2}) \[saved\][^\n]*\n([^\n]*Goal: [^\n]*))");
        auto begin = std::sregex_iterator(buf.begin(), buf.end(), header);
        auto end   = std::sregex_iterator();
        std::vector<std::pair<std::string,std::string>> hits;
        for (auto it = begin; it != end; ++it) {
            hits.emplace_back((*it)[1].str(), (*it)[2].str());
        }
        if (hits.empty()) return "";
        // Take last 3.
        std::size_t take = std::min<std::size_t>(3, hits.size());
        std::ostringstream out;
        out << "RECENT DECISIONS (session-log.md):\n";
        for (std::size_t i = hits.size() - take; i < hits.size(); ++i) {
            std::string g = hits[i].second;
            if (g.size() > 110) g = g.substr(0, 107) + "...";
            out << "  - " << hits[i].first << "  " << g << "\n";
        }
        out << "---\n";
        return out.str();
    } catch (...) { return ""; }
}

// v1.34.0 A4: drift banner. Check decisions superseded in last 24 h.
// Empty when no recent supersessions.
std::string runUserPromptDriftInject() {
    if (std::getenv("ICMG_DRIFT_INJECT_QUIET")) return "";
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        int count = 0;
        std::int64_t cutoff = static_cast<std::int64_t>(std::time(nullptr)) - 24 * 3600;
        db.query(
            "SELECT COUNT(*) FROM decisions WHERE superseded_at >= ?",
            { std::to_string(cutoff) }, [&](const Row& r){
                if (!r.empty()) { try { count = std::stoi(r[0]); } catch (...) {} }
            });
        if (count <= 0) return "";
        std::ostringstream out;
        out << "DRIFT WARNING: " << count
            << " decision(s) superseded in last 24h. Run `icmg drift status` to review.\n---\n";
        return out.str();
    } catch (...) { return ""; }
}

// v1.35.0 R8: auto-pin escalated rules. Reads RuleTelemetry::topByCount,
// prepends top-3 most-violated rule IDs + last ctx to UserPromptSubmit
// header. Forces rules the AI keeps breaking up to top of context.
// Opt-out: ICMG_R8_AUTOPIN_QUIET=1.
std::string runUserPromptEscalatedRulesInject() {
    if (std::getenv("ICMG_R8_AUTOPIN_QUIET")) return "";
    try {
        auto top = RuleTelemetry::topByCount(3);
        if (top.empty()) return "";
        std::ostringstream out;
        bool any = false;
        for (const auto& s : top) {
            if (s.count_total < 2) continue;  // ignore one-offs
            if (!any) {
                out << "ESCALATED RULES (you violated these recently — DO NOT REPEAT):\n";
                any = true;
            }
            std::string ctx = s.last_ctx;
            if (ctx.size() > 90) ctx = ctx.substr(0, 87) + "...";
            out << "  - " << s.rule_id << " (" << s.count_total << "x): " << ctx << "\n";
        }
        if (any) out << "---\n";
        return out.str();
    } catch (...) { return ""; }
}

// v1.38.0 A7: amnesia counter. Stop hook scans last AI response —
// BM25 recall over memory_nodes (last 7d window). If high-score match
// found, log amnesia event + topic. Next UserPromptSubmit will inject
// "AMNESIA WARNING" header citing prior decision.
// Opt-out: ICMG_AMNESIA_QUIET=1.
int runStopAmnesiaScan(const std::string& ai_response) {
    if (std::getenv("ICMG_AMNESIA_QUIET")) return 0;
    if (ai_response.size() < 40) return 0;
    try {
        // Extract top 3 meaningful tokens from response (4+ chars alnum).
        std::vector<std::string> toks;
        std::string cur;
        for (char c : ai_response) {
            if (std::isalnum(static_cast<unsigned char>(c)))
                cur.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            else { if (cur.size() >= 4) toks.push_back(cur); cur.clear(); }
        }
        if (cur.size() >= 4) toks.push_back(cur);
        if (toks.empty()) return 0;
        std::sort(toks.begin(), toks.end(),
            [](const auto& a, const auto& b){ return a.size() > b.size(); });
        if (toks.size() > 3) toks.resize(3);
        std::string query;
        for (auto& t : toks) { if (!query.empty()) query += " "; query += t; }
        // Recall prior decisions matching these keywords (last 7d).
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        std::int64_t cutoff = static_cast<std::int64_t>(std::time(nullptr)) - 7 * 86400;
        int hits = 0;
        std::int64_t first_id = 0;
        std::string first_topic;
        db.query(
            "SELECT id, topic FROM memory_nodes "
            "WHERE deleted_at IS NULL AND last_used >= ? "
            "AND (topic LIKE ? OR content LIKE ?) "
            "ORDER BY last_used DESC LIMIT 1",
            { std::to_string(cutoff), "%" + toks[0] + "%", "%" + toks[0] + "%" },
            [&](const Row& r){
                if (r.size() < 2) return;
                try { first_id = std::stoll(r[0]); } catch (...) {}
                first_topic = r[1];
                ++hits;
            });
        if (hits == 0) return 0;
        // Log event to global DB.
        try {
            auto& gdb = GlobalDb::instance();
            gdb.db().run(
                "INSERT INTO amnesia_events(session_id, topic, prior_node, matched_at) "
                "VALUES('', ?, ?, ?)",
                { first_topic, std::to_string(first_id),
                  std::to_string(static_cast<std::int64_t>(std::time(nullptr))) });
        } catch (...) {}
        return hits;
    } catch (...) { return 0; }
}

// v1.38.0 A7 companion: inject amnesia warning from recent unresolved events.
// Reads top-2 amnesia_events (last 24h, not yet emitted), formats compact
// header for UserPromptSubmit additionalContext.
std::string runUserPromptAmnesiaInject() {
    if (std::getenv("ICMG_AMNESIA_QUIET")) return "";
    try {
        auto& gdb = GlobalDb::instance();
        std::int64_t cutoff = static_cast<std::int64_t>(std::time(nullptr)) - 24 * 3600;
        std::ostringstream out;
        int n = 0;
        std::vector<std::int64_t> ids;
        gdb.db().query(
            "SELECT id, topic FROM amnesia_events "
            "WHERE matched_at >= ? ORDER BY matched_at DESC LIMIT 2",
            { std::to_string(cutoff) }, [&](const Row& r){
                if (r.size() < 2) return;
                if (n == 0) out << "AMNESIA WARNING (you may be re-asking already-decided things):\n";
                out << "  - prior topic match: " << r[1] << "\n";
                try { ids.push_back(std::stoll(r[0])); } catch (...) {}
                ++n;
            });
        if (n == 0) return "";
        out << "---\n";
        // Mark emitted (no schema column for that yet — skip update).
        (void)ids;
        return out.str();
    } catch (...) { return ""; }
}

// v1.38.0 Token budget enforce. PreToolUse forecast prompt token cost
// via word-count × 1.3 heuristic. Reads ~/.icmg/token-budget.json
// {"max_tokens_per_prompt": N} (default 50000). Returns 0 if OK, 1 if
// exceeded — caller blocks. Opt-out: ICMG_TOKEN_BUDGET_OFF=1.
int runPreToolUseTokenBudget(const std::string& prompt) {
    if (std::getenv("ICMG_TOKEN_BUDGET_OFF")) return 0;
    if (prompt.empty()) return 0;
    int cap = 50000;
    try {
        const char* home =
#ifdef _WIN32
            std::getenv("USERPROFILE");
#else
            std::getenv("HOME");
#endif
        if (home && *home) {
            fs::path budget = fs::path(home) / ".icmg" / "token-budget.json";
            if (fs::exists(budget)) {
                std::ifstream f(budget);
                json j; f >> j;
                cap = j.value("max_tokens_per_prompt", 50000);
            }
        }
    } catch (...) {}
    // Word count × 1.3 estimate.
    int words = 0;
    bool in_word = false;
    for (char c : prompt) {
        bool space = std::isspace(static_cast<unsigned char>(c));
        if (!space && !in_word) { in_word = true; ++words; }
        else if (space) in_word = false;
    }
    int est_tokens = static_cast<int>(words * 1.3);
    return est_tokens > cap ? 1 : 0;
}

// v1.38.0 Force-compress >2KB output. Apply Tkil-like passthrough on
// large tool outputs before AI sees them. Threshold from
// ICMG_FORCE_COMPRESS_KB env (default 2 KB). Returns empty on no
// action, otherwise compressed body. Opt-out: ICMG_NO_FORCE_COMPRESS=1.
std::string runPostToolForceCompress(const std::string& tool_output) {
    if (std::getenv("ICMG_NO_FORCE_COMPRESS")) return "";
    if (tool_output.empty()) return "";
    std::size_t kb_threshold = 2;
    if (const char* e = std::getenv("ICMG_FORCE_COMPRESS_KB")) {
        try { kb_threshold = static_cast<std::size_t>(std::stoul(e)); } catch (...) {}
    }
    if (tool_output.size() < kb_threshold * 1024) return "";
    // Compose via existing icmg compress core. For now, simple
    // line-dedup + cap-tail. Real glossary-compress is icmg::compress::
    // module (called separately).
    std::vector<std::string> lines;
    std::istringstream in(tool_output);
    std::string line;
    while (std::getline(in, line)) {
        if (!lines.empty() && lines.back() == line) continue; // dedup adj
        lines.push_back(line);
    }
    // Cap to last 50 lines + add header.
    std::ostringstream out;
    out << "[force-compress: " << tool_output.size() << " B -> ";
    std::size_t start = lines.size() > 50 ? lines.size() - 50 : 0;
    if (start > 0) out << "tail 50 / " << lines.size() << " lines]\n";
    else            out << lines.size() << " lines (dedup)]\n";
    for (std::size_t i = start; i < lines.size(); ++i) out << lines[i] << "\n";
    return out.str();
}
} // namespace icmg::core::hooks