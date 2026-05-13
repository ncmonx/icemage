// Phase 79: `icmg hook <event>` — in-process hook event handler.
//
// Consolidates the 4-5 separate icmg subprocess calls that
// .claude/hooks/icmg-prompt-recall.sh used to make per UserPromptSubmit
// event into a single icmg invocation. Saves cold-start fork overhead
// (~30-50ms × N callers = 100-200ms per prompt).
//
// Events:
//   userprompt    UserPromptSubmit — drift check + memory recall + path context
//                 + compress suggestion. Reads JSON stdin, emits additionalContext.
//
// Stdin: JSON {prompt: "...", session_id: "..."} (Claude Code event format)
// Stdout: JSON {hookSpecificOutput: {hookEventName: "...", additionalContext: "..."}}
// Exit: 0 always (hook fail-safe).
//
// Future events (deferred to next phase):
//   pretooluse-read   — replaces icmg-shrink-read.sh chain
//   posttooluse-bash  — replaces icmg-cap-output.sh
//
// Design: every step here uses in-process Db/MemoryStore/etc — no subprocess.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/path_utils.hpp"
#include "../../imem/memory_store.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "../../core/context_node_store.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

class HookCommand : public BaseCommand {
public:
    std::string name()        const override { return "hook"; }
    std::string description() const override {
        return "In-process hook event handler (userprompt/pretooluse-read/posttooluse-bash)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg hook <event> [options]\n\n"
            "Events:\n"
            "  userprompt          UserPromptSubmit — drift + recall + path-ctx merged\n"
            "                      Reads JSON stdin, emits additionalContext JSON.\n\n"
            "Designed for Claude Code .claude/hooks/ scripts: consolidates 4-5\n"
            "separate icmg invocations into one, eliminating per-prompt fork overhead.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& event = args[0];
        if (event == "userprompt")       return cmdUserPrompt();
        if (event == "pretooluse-read")  return cmdPreToolUseRead();
        std::cerr << "icmg hook: unknown event '" << event << "'\n";
        return 0;  // hook fail-safe: do not propagate non-zero
    }

private:
    static std::string lower(const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return out;
    }

    static std::string readStdinAll() {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        return ss.str();
    }

    static uint32_t fnv1a32(const std::string& s) {
        uint32_t h = 2166136261u;
        size_t n = std::min(s.size(), size_t(128));
        for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
        return h;
    }

    static std::string promptCachePath(uint32_t key) {
        char buf[9]; std::snprintf(buf, sizeof(buf), "%08x", key);
        return std::string(core::icmgGlobalDir()) + "/prompt-cache/" + buf + ".txt";
    }

    static std::string readPromptCache(uint32_t key, int ttl_sec) {
        std::string path = promptCachePath(key);
        std::ifstream f(path);
        if (!f) return "";
        std::string ts_line;
        if (!std::getline(f, ts_line)) return "";
        int64_t ts = 0;
        try { ts = std::stoll(ts_line); } catch (...) { return ""; }
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now - ts > ttl_sec) return "";
        std::ostringstream ss; ss << f.rdbuf();
        return ss.str();
    }

    static void writePromptCache(uint32_t key, const std::string& content) {
        std::string dir = std::string(core::icmgGlobalDir()) + "/prompt-cache";
        std::error_code ec; fs::create_directories(dir, ec);
        std::ofstream f(promptCachePath(key));
        if (!f) return;
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        f << now << "\n" << content;
    }

    static std::string sessionReadsPath() {
        return std::string(core::icmgGlobalDir()) + "/session-reads.txt";
    }

    static bool isFileRead(const std::string& p) {
        std::ifstream f(sessionReadsPath());
        if (!f) return false;
        std::string line;
        while (std::getline(f, line))
            if (line == p) return true;
        return false;
    }

    static void markFileRead(const std::string& p) {
        std::ofstream f(sessionReadsPath(), std::ios::app);
        if (f) f << p << "\n";
    }

    // T1: Session-dedup for injected memory/context node IDs.
    // Prevents re-injecting identical node content on every prompt in same session.
    static std::string sessionInjectedIdsPath() {
        return std::string(core::icmgGlobalDir()) + "/session-injected-ids.txt";
    }
    static bool isNodeInjected(const std::string& id) {
        std::ifstream f(sessionInjectedIdsPath());
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) if (line == id) return true;
        return false;
    }
    static void markNodeInjected(const std::string& id) {
        std::ofstream f(sessionInjectedIdsPath(), std::ios::app);
        if (f) f << id << "\n";
    }

    // Build the additionalContext payload + emit JSON to stdout.
    static void emitContext(const std::string& msg) {
        json out;
        out["hookSpecificOutput"]["hookEventName"] = "UserPromptSubmit";
        out["hookSpecificOutput"]["additionalContext"] = msg;
        std::cout << out.dump() << "\n";
    }

    // ---- userprompt handler -----------------------------------------------
    //
    // Replaces icmg-prompt-recall.sh chain. All in-process.
    int cmdUserPrompt() {
        // Phase 79: early-exit guard — env opt-out fully disables hook.
        if (std::getenv("ICMG_NO_PROMPT_HOOK")) return 0;

        std::string raw = readStdinAll();
        if (raw.empty()) return 0;

        std::string prompt;
        try {
            json j = json::parse(raw);
            if (j.contains("prompt") && j["prompt"].is_string())
                prompt = j["prompt"].get<std::string>();
            else if (j.contains("message") && j["message"].is_string())
                prompt = j["message"].get<std::string>();
        } catch (...) {
            // Malformed input — fail silent.
            return 0;
        }
        if (prompt.empty() || prompt.size() < 20) return 0;

        std::string lp = lower(prompt);
        std::ostringstream msg;

        // T4: Adaptive injection depth — scale limits with prompt complexity.
        // Short prompts (<150 chars) need minimal context; long ones get full depth.
        int recall_limit = 5;
        int ctx_limit    = 3;
        if      (prompt.size() < 150) { recall_limit = 1; ctx_limit = 1; }
        else if (prompt.size() < 500) { recall_limit = 3; ctx_limit = 2; }

        // T2: Token budget cap — stop injecting when accumulated output approaches limit.
        // Estimate: 1 token ≈ 4 chars. Cap at ~1000 tokens (4096 chars).
        constexpr size_t BUDGET_CHARS = 4096;
        auto budgetOk = [&]() -> bool {
            return (size_t)msg.tellp() < BUDGET_CHARS;
        };

        // T3: BM25 confidence threshold for memory recall — skip noisy low-score hits.
        constexpr float RECALL_MIN_SCORE = 0.15f;

        // 1. Drift check (in-process; only fires if any pinned anchor exists).
        if (!std::getenv("ICMG_NO_DRIFT_CHECK")) {
            try {
                auto& cfg = core::Config::instance();
                core::Db db(cfg.projectDbPath("."));
                // Phase 79 #2 (lite): single COUNT to skip work when 0 pinned.
                int64_t pinned_n = 0;
                try {
                    db.query("SELECT COUNT(*) FROM decisions "
                             "WHERE pinned = 1 AND superseded_at IS NULL", {},
                             [&](const core::Row& r){ if (!r.empty()) pinned_n = std::stoll(r[0]); });
                } catch (...) {}

                if (pinned_n > 0) {
                    std::vector<std::string> hits;
                    db.query(
                        "SELECT id, topic, stance, keywords FROM decisions "
                        "WHERE pinned = 1 AND superseded_at IS NULL",
                        {}, [&](const core::Row& r){
                            if (r.size() < 4) return;
                            std::string kw_lower = lower(r[3]);
                            std::string topic_lower = lower(r[1]);
                            bool match = false;
                            if (!topic_lower.empty() && lp.find(topic_lower) != std::string::npos)
                                match = true;
                            else if (!kw_lower.empty()) {
                                std::stringstream ss(kw_lower);
                                std::string tok;
                                while (std::getline(ss, tok, ',')) {
                                    size_t s = tok.find_first_not_of(" \t");
                                    size_t e = tok.find_last_not_of(" \t");
                                    if (s == std::string::npos) continue;
                                    std::string t = tok.substr(s, e - s + 1);
                                    if (!t.empty() && lp.find(t) != std::string::npos) {
                                        match = true; break;
                                    }
                                }
                            }
                            if (match)
                                hits.push_back("id=" + r[0] + " topic=\"" + r[1]
                                               + "\" stance=\"" + r[2] + "\"");
                        });
                    if (!hits.empty()) {
                        msg << "[icmg drift] prompt touches " << hits.size()
                            << " pinned decision(s):\n";
                        for (auto& h : hits) msg << "  - " << h << "\n";
                        msg << "Verify direction aligns. Override with `icmg drift supersede`.\n\n";
                    }
                }
            } catch (...) {}
        }

        // 2. Memory recall — top N hits (adaptive limit, T3 threshold, T1 dedup).
        int local_hits = 0;
        if (budgetOk()) try {
            auto& cfg = core::Config::instance();
            core::Db db(cfg.projectDbPath("."));
            imem::MemoryStore mem(db);
            auto results = mem.recall(prompt, recall_limit, false);
            std::ostringstream rec_out;
            int added = 0;
            for (auto& m : results) {
                if (m.score < RECALL_MIN_SCORE) continue;           // T3: threshold
                std::string nid = std::to_string(m.id);
                if (isNodeInjected(nid)) continue;                   // T1: dedup
                markNodeInjected(nid);
                std::string topic = m.topic;
                if (topic.size() > 80) topic = topic.substr(0, 77) + "...";
                rec_out << "  [" << static_cast<int>(m.score) << "] " << topic << "\n";
                ++added;
            }
            if (added > 0) {
                local_hits = added;
                msg << "icmg memory hits (proactively surfaced):\n" << rec_out.str() << "\n";
            }
        } catch (...) {}

        // 2b. Cross-project recall fallback when local hits sparse.
        if (budgetOk() && local_hits < 2 && std::getenv("ICMG_CROSS_PROJECT") == nullptr) {
            try {
                std::string gdb = std::string(core::icmgGlobalDir()) + "/global.db";
                core::Db gd(gdb);
                std::vector<std::pair<std::string, std::string>> projs;
                gd.query("SELECT name, path FROM projects", {},
                         [&](const core::Row& r){
                             if (r.size() >= 2) projs.push_back({r[0], r[1]});
                         });
                std::ostringstream xmsg;
                int xhits = 0;
                std::string cwd = fs::current_path().string();
                for (auto& [pname, ppath] : projs) {
                    if (!budgetOk() || xhits >= 3) break;
                    if (ppath == cwd) continue;
                    std::string pdb = ppath + "/.icmg/data.db";
                    if (!fs::exists(pdb)) continue;
                    try {
                        core::Db pd(pdb);
                        imem::MemoryStore pm(pd);
                        auto pres = pm.recall(prompt, 2, false);
                        for (auto& m : pres) {
                            if (xhits >= 3 || !budgetOk()) break;
                            if (m.score < RECALL_MIN_SCORE) continue;     // T3
                            std::string xid = "x:" + pname + ":" + std::to_string(m.id);
                            if (isNodeInjected(xid)) continue;             // T1
                            markNodeInjected(xid);
                            std::string topic = m.topic;
                            if (topic.size() > 70) topic = topic.substr(0, 67) + "...";
                            xmsg << "  [" << pname << "][" << (int)m.score
                                 << "] " << topic << "\n";
                            ++xhits;
                        }
                    } catch (...) {}
                }
                if (xhits > 0) {
                    msg << "cross-project recall (other projects):\n"
                        << xmsg.str() << "\n";
                }
            } catch (...) {}
        }

        // 2c. Path-context — detect file path mentions, emit graph context.
        if (budgetOk()) {
            // Match common code file extensions.
            static const std::vector<std::string> exts = {
                ".cs", ".ts", ".tsx", ".js", ".jsx", ".py", ".cpp", ".hpp",
                ".c", ".h", ".rb", ".go", ".rs", ".sql", ".md", ".json",
                ".yaml", ".yml"
            };
            std::string firstpath;
            for (auto& ext : exts) {
                size_t p = lp.find(ext);
                if (p == std::string::npos) continue;
                // Walk backwards for path-like chars.
                size_t end = p + ext.size();
                size_t start = p;
                while (start > 0) {
                    char c = prompt[start - 1];
                    if (std::isalnum((unsigned char)c) || c == '_' || c == '/'
                        || c == '\\' || c == '.' || c == '-') --start;
                    else break;
                }
                if (end > start && end - start <= 256) {
                    firstpath = prompt.substr(start, end - start);
                    break;
                }
            }
            if (!firstpath.empty() && fs::exists(firstpath)) {
                try {
                    auto& cfg = core::Config::instance();
                    core::Db db(cfg.projectDbPath("."));
                    std::string lang, ctx;
                    int symbol_count = 0;
                    db.query("SELECT lang, context FROM graph_nodes WHERE path = ?",
                             {firstpath}, [&](const core::Row& r){
                                 if (r.size() >= 2) { lang = r[0]; ctx = r[1]; }
                             });
                    db.query("SELECT COUNT(*) FROM graph_nodes WHERE parent_id IN "
                             "(SELECT id FROM graph_nodes WHERE path = ?)",
                             {firstpath}, [&](const core::Row& r){
                                 if (!r.empty()) symbol_count = std::stoi(r[0]);
                             });
                    if (!lang.empty() || symbol_count > 0) {
                        msg << "icmg path-context for " << firstpath << ":\n";
                        if (!lang.empty()) msg << "  lang: " << lang << "\n";
                        if (symbol_count > 0)
                            msg << "  symbols: " << symbol_count << "\n";
                        if (!ctx.empty()) {
                            std::string trunc = ctx.size() > 200 ? ctx.substr(0, 197) + "..." : ctx;
                            msg << "  context: " << trunc << "\n";
                        }
                        msg << "  → for full slice: `icmg context " << firstpath
                            << " --lines A-B`\n\n";
                    }
                    // T9: 1-hop BFS — callers/callees via graph_edges (file→symbol→neighbor).
                    if (budgetOk()) {
                        std::ostringstream bfs_out;
                        int bfs_n = 0;
                        db.query(
                            "SELECT DISTINCT n2.path, e.edge_type "
                            "FROM graph_edges e "
                            "JOIN graph_nodes n1 ON n1.id = e.src "
                            "JOIN graph_nodes n2 ON n2.id = e.dst "
                            "WHERE n1.path = ? AND n2.path != ? LIMIT 5",
                            {firstpath, firstpath},
                            [&](const core::Row& r){
                                if (r.size() < 2 || bfs_n >= 5) return;
                                bfs_out << "  →" << r[1] << ": " << r[0] << "\n";
                                ++bfs_n;
                            });
                        if (bfs_n > 0)
                            msg << "icmg graph-neighbors (" << firstpath << "):\n"
                                << bfs_out.str() << "\n";
                    }
                } catch (...) {}
            }
        }

        // 2d. T9: BFS 1-hop expansion — callers/callees of the path-context file.
        // Seeds from graph_nodes matching the detected path, expands via graph_edges depth-1.
        // Skipped if budget is tight or path-context found nothing.
        if (budgetOk() && !lp.empty()) {
            // Reuse the firstpath detection from 2c (re-detect quickly).
            static const std::vector<std::string> bfs_exts = {
                ".cs",".ts",".tsx",".js",".py",".cpp",".hpp",".c",".h",".go",".rs"
            };
            std::string bfspath;
            for (auto& ext : bfs_exts) {
                size_t p = lp.find(ext);
                if (p == std::string::npos) continue;
                size_t end = p + ext.size(), start = p;
                while (start > 0) {
                    char c = prompt[start-1];
                    if (std::isalnum((unsigned char)c)||c=='_'||c=='/'||c=='\\'||c=='.'||c=='-') --start;
                    else break;
                }
                if (end > start && end - start <= 256) { bfspath = prompt.substr(start, end-start); break; }
            }
            if (!bfspath.empty() && fs::exists(bfspath)) {
                try {
                    auto& cfg = core::Config::instance();
                    core::Db db(cfg.projectDbPath("."));
                    // Get node id for seed file.
                    int64_t seed_id = 0;
                    db.query("SELECT id FROM graph_nodes WHERE path = ? LIMIT 1",
                             {bfspath}, [&](const core::Row& r){
                                 if (!r.empty()) try { seed_id = std::stoll(r[0]); } catch (...) {}
                             });
                    if (seed_id > 0) {
                        std::ostringstream bfs_out;
                        int bfs_added = 0;
                        db.query("SELECT n.path, e.edge_type FROM graph_edges e "
                                 "JOIN graph_nodes n ON n.id = e.dst "
                                 "WHERE e.src = ? AND e.edge_type IN ('calls','imports','uses') LIMIT 5",
                                 {std::to_string(seed_id)},
                                 [&](const core::Row& r){
                                     if (r.size() < 2 || !budgetOk()) return;
                                     std::string bkey = "bfs:" + r[0];
                                     if (isNodeInjected(bkey)) return;
                                     markNodeInjected(bkey);
                                     bfs_out << "  " << r[1] << " → " << r[0] << "\n";
                                     ++bfs_added;
                                 });
                        if (bfs_added > 0)
                            msg << "icmg bfs-context (1-hop from " << bfspath << "):\n"
                                << bfs_out.str() << "\n";
                    }
                } catch (...) {}
            }
        }

        // 3. Compress suggestion — large prompt heuristic.
        size_t sz = prompt.size();
        if (sz > 4000) {
            msg << "(Large prompt " << sz
                << "B — pipe big paste through `icmg compress` next time.)\n";
        }

        // 4. Cold context_nodes + skill injection — BM25 min-score 0.15, cached 300s (T12).
        if (budgetOk() && !prompt.empty() && !std::getenv("ICMG_NO_CONTEXT_HOOK")) {
            uint32_t h = fnv1a32(prompt);
            std::string cached_ctx = readPromptCache(h, 300);
            if (!cached_ctx.empty()) {
                msg << cached_ctx;
            } else {
                try {
                    auto& cfg = core::Config::instance();
                    core::Db db(cfg.projectDbPath("."));
                    core::ContextNodeStore cns(db);
                    auto cold  = cns.search(prompt, "cold",  ctx_limit, 0.15f);
                    auto skill = cns.search(prompt, "skill", 2,         0.15f);
                    std::ostringstream ctx_out;
                    for (auto& n : cold) {
                        if (!budgetOk()) break;
                        if (isNodeInjected(n.node_key)) continue;        // T1
                        markNodeInjected(n.node_key);
                        // T10: compress content — signature only (≤200 chars).
                        std::string body = n.content.size() > 200
                                         ? n.content.substr(0, 197) + "..."
                                         : n.content;
                        ctx_out << "[ctx:" << n.node_key << "] " << n.title
                                << "\n" << body << "\n\n";
                    }
                    if (!skill.empty()) {
                        ctx_out << "---\nSuggested skills:";
                        for (auto& n : skill) ctx_out << " " << n.title << ";";
                        ctx_out << "\n";
                    }
                    std::string result = ctx_out.str();
                    if (!result.empty()) {
                        writePromptCache(h, result);
                        msg << result;
                    }
                } catch (...) {}
            }
        }

        if (msg.tellp() == 0) return 0;
        emitContext(msg.str());
        return 0;
    }

    // ---- pretooluse-read handler ------------------------------------------
    //
    // Replaces icmg-shrink-read.sh chain. Emits permissionDecision=allow
    // with capped updatedInput.limit + icmg context overlay as additionalContext.
    int cmdPreToolUseRead() {
        if (std::getenv("ICMG_NO_READ_HOOK")) return 0;
        std::string raw = readStdinAll();
        if (raw.empty()) return 0;

        std::string file_path;
        int orig_offset = 0, orig_limit = 0;
        try {
            json j = json::parse(raw);
            if (j.contains("tool_input")) {
                auto& ti = j["tool_input"];
                if (ti.contains("file_path") && ti["file_path"].is_string())
                    file_path = ti["file_path"].get<std::string>();
                if (ti.contains("offset") && ti["offset"].is_number())
                    orig_offset = ti["offset"].get<int>();
                if (ti.contains("limit") && ti["limit"].is_number())
                    orig_limit = ti["limit"].get<int>();
            }
        } catch (...) { return 0; }
        if (file_path.empty() || !fs::exists(file_path)) return 0;

        // Session dedup — emit reminder if file already read this session.
        bool already_read = isFileRead(file_path);
        markFileRead(file_path);
        if (already_read) {
            json out;
            out["hookSpecificOutput"]["hookEventName"] = "PreToolUse";
            out["hookSpecificOutput"]["permissionDecision"] = "allow";
            out["hookSpecificOutput"]["updatedInput"]["file_path"] = file_path;
            out["hookSpecificOutput"]["additionalContext"] =
                "[dedup] " + file_path + " already read this session. "
                "Use `icmg context " + file_path + " --lines A-B` for targeted slice, "
                "or confirm re-read is needed.";
            std::cout << out.dump() << "\n";
            return 0;
        }

        // Phase 79: force-icmg-read default ON (toggle off via ICMG_NO_READ_FORCE).
        bool force = std::getenv("ICMG_NO_READ_FORCE") == nullptr;

        // Size + cap decisions.
        uintmax_t sz = 0;
        try { sz = fs::file_size(file_path); } catch (...) {}
        int default_threshold = 20000;
        const char* thr_env = std::getenv("ICMG_SHRINK_THRESHOLD");
        if (thr_env) {
            try { default_threshold = std::stoi(thr_env); } catch (...) {}
        }
        if (!force && (orig_offset > 0 || orig_limit > 0)) return 0;  // user knows what they want
        if (!force && (int64_t)sz < default_threshold) return 0;

        int cap = 30;
        const char* cap_env = std::getenv("ICMG_READ_LIMIT");
        if (cap_env) {
            try { cap = std::stoi(cap_env); } catch (...) {}
        }

        // Build overlay: file metadata from graph + symbol count.
        std::ostringstream ctx;
        try {
            auto& cfg = core::Config::instance();
            core::Db db(cfg.projectDbPath("."));
            std::string lang, gctx;
            int64_t fsize = 0;
            db.query("SELECT lang, context, size_bytes FROM graph_nodes WHERE path = ?",
                     {file_path}, [&](const core::Row& r){
                         if (r.size() >= 3) { lang = r[0]; gctx = r[1];
                             try { fsize = std::stoll(r[2]); } catch (...) {}
                         }
                     });
            int symbol_count = 0;
            db.query("SELECT COUNT(*) FROM graph_nodes WHERE parent_id IN "
                     "(SELECT id FROM graph_nodes WHERE path = ?)",
                     {file_path}, [&](const core::Row& r){
                         if (!r.empty()) symbol_count = std::stoi(r[0]);
                     });
            ctx << "STRICT: file " << file_path << " is " << sz << " bytes. "
                << "Read CAPPED to " << cap << " lines (Edit-anchor only). "
                << "For full slice: `icmg context " << file_path
                << " --lines A-B`. For symbol body: `icmg graph symbol <Name>`.\n";
            if (!lang.empty()) ctx << "lang: " << lang << "\n";
            if (symbol_count > 0) ctx << "symbols: " << symbol_count << "\n";
            if (!gctx.empty()) {
                std::string g = gctx.size() > 400 ? gctx.substr(0, 397) + "..." : gctx;
                ctx << "context: " << g << "\n";
            }
        } catch (...) {}

        json out;
        out["hookSpecificOutput"]["hookEventName"] = "PreToolUse";
        out["hookSpecificOutput"]["permissionDecision"] = "allow";
        out["hookSpecificOutput"]["updatedInput"]["file_path"] = file_path;
        out["hookSpecificOutput"]["updatedInput"]["limit"] = cap;
        out["hookSpecificOutput"]["additionalContext"] = ctx.str();
        std::cout << out.dump() << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("hook", HookCommand);

} // namespace icmg::cli
