// Phase 79: `icmg hook <event>` â€” in-process hook event handler.
//
// Consolidates the 4-5 separate icmg subprocess calls that
// .claude/hooks/icmg-prompt-recall.sh used to make per UserPromptSubmit
// event into a single icmg invocation. Saves cold-start fork overhead
// (~30-50ms Ã— N callers = 100-200ms per prompt).
//
// Events:
//   userprompt    UserPromptSubmit â€” drift check + memory recall + path context
//                 + compress suggestion. Reads JSON stdin, emits additionalContext.
//
// Stdin: JSON {prompt: "...", session_id: "..."} (Claude Code event format)
// Stdout: JSON {hookSpecificOutput: {hookEventName: "...", additionalContext: "..."}}
// Exit: 0 always (hook fail-safe).
//
// Future events (deferred to next phase):
//   pretooluse-read   â€” replaces icmg-shrink-read.sh chain
//   posttooluse-bash  â€” replaces icmg-cap-output.sh
//
// Design: every step here uses in-process Db/MemoryStore/etc â€” no subprocess.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/path_utils.hpp"
#include "../../core/json_safe.hpp"   // v1.68.1 safeDump (UTF-8 crash-safe)
#include "../../core/exec_utils.hpp"
#include "../../core/hooks/runners.hpp"
#include "../../core/hooks/precompact_output.hpp"  // v1.75.1: PreCompact schema guard
#include "../../core/hooks/internals.hpp"
#include "../../daemon/rule_daemon_client.hpp"
#include "../../imem/memory_store.hpp"
#include "../../compress/write_expander.hpp"   // v1.25.0 (W3)

#ifdef _WIN32
#  include <process.h>
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  define ICMG_GETPID() _getpid()
#else
#  include <unistd.h>
#  define ICMG_GETPID() getpid()
#endif

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
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
            "  userprompt          UserPromptSubmit â€” drift + recall + path-ctx merged\n"
            "                      Reads JSON stdin, emits additionalContext JSON.\n\n"
            "Designed for Claude Code .claude/hooks/ scripts: consolidates 4-5\n"
            "separate icmg invocations into one, eliminating per-prompt fork overhead.\n";
    }

    // v1.21.8: per-event injection cache. When the dispatch budget is
    // exceeded, serve the last successful result instead of dropping the
    // injection entirely. The background future continues running and will
    // refresh the cache for the NEXT invocation — so the slow path
    // self-heals after one timeout.
    static std::filesystem::path injectionCachePath(const std::string& event) {
        namespace fs = std::filesystem;
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        return fs::path(home ? home : ".") / ".icmg"
             / (std::string("hook-cache-") + event + ".txt");
    }

    static void writeInjectionCache(const std::string& event,
                                    const std::string& payload) {
        namespace fs = std::filesystem;
        try {
            auto p = injectionCachePath(event);
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream(p, std::ios::binary) << payload;
        } catch (...) {}
    }

    static std::string readInjectionCache(const std::string& event,
                                          int max_age_seconds = 60) {
        namespace fs = std::filesystem;
        try {
            auto p = injectionCachePath(event);
            if (!fs::exists(p)) return "";
            auto mtime = fs::last_write_time(p);
            auto sys_now = std::chrono::system_clock::now();
            auto fs_now  = fs::file_time_type::clock::now();
            auto age_clk = fs_now - mtime;
            auto age = std::chrono::duration_cast<std::chrono::seconds>(age_clk).count();
            (void)sys_now;
            if (age > max_age_seconds) return "";
            std::ifstream f(p, std::ios::binary);
            std::ostringstream ss; ss << f.rdbuf();
            std::string cached = ss.str();
            // v1.75.1: never serve a precompact cache entry that fails
            // Claude Code's PreCompact schema (e.g. stale pre-1.75.1 output
            // using hookSpecificOutput.additionalContext) -> force fresh emit.
            if (event == "precompact" &&
                !icmg::core::hooks::isValidPreCompactOutput(cached))
                return "";
            return cached;
        } catch (...) { return ""; }
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& event = args[0];

        // v1.6.2: wrap dispatch with timeout. Default 500ms (override via
        // ICMG_HOOK_TIMEOUT_MS). On timeout, v1.21.8 serves the cached
        // injection (≤60s old) rather than dropping it — keeps the prompt
        // useful even when DB lock contention or cold-start eats the budget.
        int timeout_ms = 500;
        if (const char* env = std::getenv("ICMG_HOOK_TIMEOUT_MS")) {
            try { timeout_ms = std::stoi(env); } catch (...) {}
        }
        if (timeout_ms <= 0) {
            // Disabled — direct dispatch + still update cache for next call.
            std::ostringstream captured;
            auto* old = std::cout.rdbuf(captured.rdbuf());
            int rc = dispatch(event);
            std::cout.rdbuf(old);
            std::cout << captured.str();
            writeInjectionCache(event, captured.str());
            return rc;
        }

        // Stale-while-revalidate pattern (HTTP-cache style). If a recent
        // cache exists, serve it INSTANTLY and refresh in the background.
        // If no cache yet, fall through to synchronous dispatch with the
        // timeout budget. After timeout, serve stale cache as last resort.
        //
        // This makes the hook effectively multi-tasked: every call returns
        // fast (≤ a few ms when warm), the slow dispatch runs in parallel
        // with the user's next reasoning step, and the cache rolls forward.
        //
        // Disable via ICMG_HOOK_NO_CACHE=1 (forces sync path every call).
        bool use_cache = !std::getenv("ICMG_HOOK_NO_CACHE");
        int  cache_max_age = 30;  // seconds — fresh-while-warm window
        if (const char* env = std::getenv("ICMG_HOOK_CACHE_MAX_AGE")) {
            try { cache_max_age = std::stoi(env); } catch (...) {}
        }

        std::string ev_copy = event;

        if (use_cache) {
            std::string fresh_cached = readInjectionCache(event, cache_max_age);
            if (!fresh_cached.empty()) {
                std::cout << fresh_cached;
                // Background refresh: do not delay user — fire+forget.
                std::thread([this, ev_copy]() {
                    std::ostringstream local;
                    auto* old = std::cout.rdbuf(local.rdbuf());
                    (void)dispatch(ev_copy);
                    std::cout.rdbuf(old);
                    writeInjectionCache(ev_copy, local.str());
                }).detach();
                return 0;
            }
        }

        // No fresh cache → synchronous dispatch with the timeout budget. The
        // worker writes cache from inside its lambda so even on timeout the
        // next call benefits.
        auto sh_out = std::make_shared<std::string>();
        auto sh_mu  = std::make_shared<std::mutex>();
        auto prom   = std::make_shared<std::promise<int>>();
        std::future<int> fut = prom->get_future();

        std::thread([this, ev_copy, prom, sh_out, sh_mu]() {
            std::ostringstream local;
            int rc = 0;
            {
                std::lock_guard<std::mutex> lk(*sh_mu);
                auto* old = std::cout.rdbuf(local.rdbuf());
                rc = dispatch(ev_copy);
                std::cout.rdbuf(old);
                *sh_out = local.str();
            }
            writeInjectionCache(ev_copy, local.str());
            try { prom->set_value(rc); } catch (...) {}
        }).detach();

        if (fut.wait_for(std::chrono::milliseconds(timeout_ms))
                == std::future_status::ready) {
            int rc = fut.get();
            std::string out;
            {
                std::lock_guard<std::mutex> lk(*sh_mu);
                out = *sh_out;
            }
            std::cout << out;
            return rc;
        }

        // Timeout AND no fresh cache → try stale cache as last resort.
        // IMPORTANT: at this point the detached worker may still hold the
        // cout rdbuf swap, so `std::cout << ...` could write into the
        // worker's local stringstream instead of the real stdout. Use a
        // raw OS write to fd 1 to bypass C++ iostream state.
        std::string stale = readInjectionCache(event, /*max_age=*/3600);
        if (!stale.empty()) {
            rawWriteStdout(stale);
            std::cerr << "icmg hook " << event << ": timeout " << timeout_ms
                      << "ms — served stale cache (" << stale.size()
                      << " bytes); background refresh running\n";
        } else {
            std::cerr << "icmg hook " << event << ": timeout " << timeout_ms
                      << "ms — no cache yet (first slow run); background continues\n";
        }
        return 0;
    }

    static void rawWriteStdout(const std::string& s) {
#ifdef _WIN32
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h && h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(h, s.data(), (DWORD)s.size(), &written, nullptr);
        }
#else
        ssize_t n = ::write(1, s.data(), s.size());
        (void)n;
#endif
    }

    int dispatch(const std::string& event) {
        if (event == "userprompt")          return cmdUserPrompt();
        if (event == "pretooluse-read")     return cmdPreToolUseRead();
        if (event == "pretooluse")          return cmdPreToolUseEnforce();
        if (event == "stop")                return cmdStop();
        if (event == "precompact")          return cmdPreCompact();
        if (event == "posttooluse-read")    return cmdPostToolUseRead();
        if (event == "posttooluse-bash")    return cmdPostToolUseBash();
        if (event == "pretooluseedit")      return cmdPreToolUseEditDisambig();
        if (event == "posttooluse-edit")    return cmdPostToolUseEditAutoSync();
        if (event == "pretooluse-write")    return cmdPreToolUseWrite();
        std::cerr << "icmg hook: unknown event '" << event << "'\n";
        return 0;
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

    // v1.28.0 #E: dedup with 5-min TTL. Previously session-reads.txt
    // grew unbounded and any prior-read file was suppressed forever in
    // the session — even when content had changed. Now each line is
    // `<unix_ts>\t<path>`; entries older than the TTL window are ignored
    // on read; legacy untimestamped lines still match once for compat.
    static constexpr int64_t kReadDedupTTL = 300;  // 5 min

    static bool isFileRead(const std::string& p) {
        std::ifstream f(sessionReadsPath());
        if (!f) return false;
        int64_t now = (int64_t)std::time(nullptr);
        std::string line;
        while (std::getline(f, line)) {
            auto tab = line.find('\t');
            if (tab == std::string::npos) {
                // Legacy untimestamped entry — match path, treat as fresh once.
                if (line == p) return true;
                continue;
            }
            int64_t ts = 0;
            try { ts = std::stoll(line.substr(0, tab)); } catch (...) { continue; }
            if (now - ts > kReadDedupTTL) continue;
            if (line.substr(tab + 1) == p) return true;
        }
        return false;
    }

    static void markFileRead(const std::string& p) {
        std::ofstream f(sessionReadsPath(), std::ios::app);
        if (f) f << (int64_t)std::time(nullptr) << "\t" << p << "\n";
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

    // v1.33.0 R5: ship-checklist injection helper.
    static std::string shipChecklistIfRelevant(const std::string& prompt) {
        if (prompt.empty()) return "";
        std::string lc = prompt;
        for (char& c : lc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bool match = false;
        static const char* kw[] = {
            " ship ", " release ", "bump version", "bump v", "publish v",
            "tag v", "release v", "build linux", "build win", "gh release"
        };
        for (const char* k : kw) if (lc.find(k) != std::string::npos) { match = true; break; }
        if (!match) for (const char* k : { "ship ", "release ", "publish " })
            if (lc.rfind(k, 0) == 0) { match = true; break; }
        if (!match) return "";
        return
            "MANDATORY SHIP WORKFLOW (phases gated):\n"
            "  1. icmg ship start v<X.Y.Z>\n"
            "  2. icmg ship build / test\n"
            "  3. icmg ship pack-win + pack-linux (BOTH required)\n"
            "  4. icmg ship push-private + docs-pr\n"
            "  5. icmg ship publish (refuses if any prior phase missing/stale)\n"
            "Cross-platform rule: never ship win-only.\n---\n";
    }

    // Build the additionalContext payload + emit JSON to stdout.
    // v1.1.0 Task 6.6: prepend sayless re-inject block when sayless.flag is
    // ON and violations >0. Empty block on healthy sessions â†’ no overhead.
    // v1.3.0 Task 7: prepend skill chunk hint when top score â‰¥ 0.20.
    static void emitContext(const std::string& msg, const std::string& prompt = "") {
        std::string sayless = icmg::core::hooks::runUserPromptSaylessInject();
        std::string skill_hint = prompt.empty()
            ? ""
            : icmg::core::hooks::runUserPromptSkillSuggest(prompt);
        json out;
        out["hookSpecificOutput"]["hookEventName"] = "UserPromptSubmit";
        // v1.4.0 T5: approach-history inject.
        std::string approach_hint = prompt.empty()
            ? ""
            : icmg::core::hooks::runUserPromptApproachInject(prompt);
        std::string ship_hint = shipChecklistIfRelevant(prompt);
        std::string rules_hint    = icmg::core::hooks::runUserPromptPinnedRulesInject();
        std::string projects_hint = icmg::core::hooks::runUserPromptProjectsInject();
        std::string known_hint    = icmg::core::hooks::runUserPromptKnownIssueInject(prompt);
        std::string fail_hint     = icmg::core::hooks::runUserPromptFailInject(prompt);
        std::string decisions_hint= icmg::core::hooks::runUserPromptRecentDecisionsInject();
        std::string drift_hint    = icmg::core::hooks::runUserPromptDriftInject();
        std::string escalated_hint= icmg::core::hooks::runUserPromptEscalatedRulesInject();
        std::string amnesia_hint  = icmg::core::hooks::runUserPromptAmnesiaInject();
        std::string budget_hint   = (icmg::core::hooks::runPreToolUseTokenBudget(prompt) > 0) ? std::string("TOKEN BUDGET WARN. ICMG_TOKEN_BUDGET_OFF=1 to disable. ") : std::string("");
        out["hookSpecificOutput"]["additionalContext"] = budget_hint + amnesia_hint + escalated_hint + drift_hint + rules_hint + projects_hint + known_hint + fail_hint + decisions_hint + ship_hint + approach_hint + skill_hint + sayless + msg;
        std::cout << icmg::core::safeDump(out) << "\n";
    }

    // ---- userprompt handler -----------------------------------------------
    //
    // Replaces icmg-prompt-recall.sh chain. All in-process.
    int cmdUserPrompt() {
        // Phase 79: early-exit guard â€” env opt-out fully disables hook.
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
            // Malformed input â€” fail silent.
            return 0;
        }
        if (prompt.empty() || prompt.size() < 20) return 0;

        std::string lp = lower(prompt);
        std::ostringstream msg;

        // T4: Adaptive injection depth â€” scale limits with prompt complexity.
        // Short prompts (<150 chars) need minimal context; long ones get full depth.
        int recall_limit = 5;
        int ctx_limit    = 3;
        if      (prompt.size() < 150) { recall_limit = 1; ctx_limit = 1; }
        else if (prompt.size() < 500) { recall_limit = 3; ctx_limit = 2; }

        // T2: Token budget cap â€” stop injecting when accumulated output approaches limit.
        // Estimate: 1 token â‰ˆ 4 chars. Cap at ~1000 tokens (4096 chars).
        constexpr size_t BUDGET_CHARS = 4096;
        auto budgetOk = [&]() -> bool {
            return (size_t)msg.tellp() < BUDGET_CHARS;
        };

        // T3: BM25 confidence threshold for memory recall â€” skip noisy low-score hits.
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

        // 2. Memory recall â€” top N hits (adaptive limit, T3 threshold, T1 dedup).
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

        // 2c. Path-context â€” detect file path mentions, emit graph context.
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
                        msg << "  â†’ for full slice: `icmg context " << firstpath
                            << " --lines A-B`\n\n";
                    }
                    // T9: 1-hop BFS â€” callers/callees via graph_edges (fileâ†’symbolâ†’neighbor).
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
                                bfs_out << "  â†’" << r[1] << ": " << r[0] << "\n";
                                ++bfs_n;
                            });
                        if (bfs_n > 0)
                            msg << "icmg graph-neighbors (" << firstpath << "):\n"
                                << bfs_out.str() << "\n";
                    }
                } catch (...) {}
            }
        }

        // 2d. T9: BFS 1-hop expansion â€” callers/callees of the path-context file.
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
                                     bfs_out << "  " << r[1] << " â†’ " << r[0] << "\n";
                                     ++bfs_added;
                                 });
                        if (bfs_added > 0)
                            msg << "icmg bfs-context (1-hop from " << bfspath << "):\n"
                                << bfs_out.str() << "\n";
                    }
                } catch (...) {}
            }
        }

        // 3. Compress suggestion â€” large prompt heuristic.
        size_t sz = prompt.size();
        if (sz > 4000) {
            msg << "(Large prompt " << sz
                << "B â€” pipe big paste through `icmg compress` next time.)\n";
        }

        // 4. Cold context_nodes + skill injection â€” BM25 min-score 0.15, cached 300s (T12).
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
                        // T10: compress content â€” signature only (â‰¤200 chars).
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
        emitContext(msg.str(), prompt);
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

        // Session dedup â€” emit reminder if file already read this session.
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
            std::cout << icmg::core::safeDump(out) << "\n";
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

        // v1.21.1 (F8): auto-spill for very large files (>50KB default).
        // Write full content to .icmg/spill/<ts>_<basename>.txt and emit a
        // pointer in additionalContext. LLM only pays tokens for the pointer
        // line unless it explicitly reads the spill file. Threshold tunable
        // via ICMG_AUTO_SPILL_THRESHOLD env (bytes). Disable: =0.
        {
            int64_t spill_threshold = 51200;
            const char* st = std::getenv("ICMG_AUTO_SPILL_THRESHOLD");
            if (st) { try { spill_threshold = std::stoll(st); } catch (...) {} }
            if (spill_threshold > 0 && (int64_t)sz >= spill_threshold) {
                std::error_code ec;
                fs::path spill_dir = fs::path(".icmg") / "spill";
                fs::create_directories(spill_dir, ec);
                auto t = std::chrono::system_clock::now().time_since_epoch();
                auto ts = std::chrono::duration_cast<std::chrono::seconds>(t).count();
                fs::path src(file_path);
                std::string base = src.filename().string();
                fs::path spill = spill_dir / (std::to_string(ts) + "_" + base);
                std::error_code ec2;
                fs::copy_file(file_path, spill, fs::copy_options::overwrite_existing, ec2);
                if (!ec2) {
                    json out;
                    out["hookSpecificOutput"]["hookEventName"] = "PreToolUse";
                    out["hookSpecificOutput"]["permissionDecision"] = "allow";
                    out["hookSpecificOutput"]["updatedInput"]["file_path"] = file_path;
                    out["hookSpecificOutput"]["updatedInput"]["limit"] = 50;
                    out["hookSpecificOutput"]["additionalContext"] =
                        "[F8 auto-spill] file " + std::to_string((long long)sz)
                        + "B > threshold; full copy saved to " + spill.string()
                        + ". Default Read capped to first 50 lines; expand only if needed.";
                    std::cout << icmg::core::safeDump(out) << "\n";
                    return 0;
                }
            }
        }

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
        std::cout << icmg::core::safeDump(out) << "\n";
        return 0;
    }

    // v0.54.0: Stop / PreCompact / PostToolUse-Read handlers (defined below).
    int cmdStop();
    int cmdPreCompact();
    int cmdPostToolUseRead();
    // v1.1.0 Task 6: PreToolUse hard-deny enforcement.
    int cmdPreToolUseEnforce();
    // v1.3.0 Task 13: PostToolUse:Bash test-fail auto-context.
    int cmdPostToolUseBash();
    // v1.4.0 Task 1: PreToolUse:Edit target disambiguation.
    int cmdPreToolUseEditDisambig();
    // v1.4.0 Task 3: PostToolUse:Edit/Write auto graph-update + memory draft.
    int cmdPostToolUseEditAutoSync();
    // v1.25.0 (W1/W3): PreToolUse:Write expander — detects compressed-write
    // magic header in tool_input.content and emits updatedInput with the
    // expanded bytes. Pass-through if no header / flag absent / parse fail.
    int cmdPreToolUseWrite();
};

// â”€â”€ v0.56.0: Stop / PreCompact / PostToolUse-Read â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//
// Logic extracted to `core/hooks/runners.{hpp,cpp}` (Phase B.B3). Each cmd:
//   1. Try the rule-daemon RPC path (saves ~30-50ms icmg.exe startup Ã— event).
//   2. Fall back to inline runner if daemon down or RPC fails.
//
// Single source of truth â€” daemon handler + inline path call the same runner.

int HookCommand::cmdStop() {
    std::string raw = readStdinAll();
    if (icmg::daemon::RuleDaemonClient::callHook("hook_stop", raw)) return 0;
    icmg::core::hooks::runStopHook(raw);
    return 0;
}

int HookCommand::cmdPreCompact() {
    std::string raw = readStdinAll();
    std::string emit;
    if (icmg::daemon::RuleDaemonClient::callHook("hook_precompact", raw, &emit)) {
        if (!emit.empty()) std::cout << emit << "\n";
        return 0;
    }
    auto out = icmg::core::hooks::runPreCompactHook(raw);
    if (!out.empty()) std::cout << out << "\n";
    return 0;
}

int HookCommand::cmdPostToolUseRead() {
    std::string raw = readStdinAll();
    std::string emit;
    if (icmg::daemon::RuleDaemonClient::callHook("hook_posttool_read", raw, &emit)) {
        if (!emit.empty()) std::cout << emit << "\n";
        return 0;
    }
    auto out = icmg::core::hooks::runPostToolUseReadHook(raw);
    if (!out.empty()) std::cout << out << "\n";
    return 0;
}

// v1.1.0 Task 6: PreToolUse hard-deny â€” reads Claude Code hook v2 event from
// stdin, returns permissionDecision JSON. Bypassable per-session via
// ICMG_STRICT_BYPASS=1.
int HookCommand::cmdPreToolUseEnforce() {
    std::string raw = readStdinAll();
    // v1.4.0 Task 2: check git guard first (file-path forms of git checkout/restore/reset).
    std::string git_guard = icmg::core::hooks::runPreToolUseBashGitGuard(raw);
    if (git_guard.find("\"deny\"") != std::string::npos) {
        std::cout << git_guard << "\n";
        return 0;
    }
    std::string out = icmg::core::hooks::runPreToolUseEnforce(raw);
    if (!out.empty()) std::cout << out << "\n";
    return 0;
}

// v1.3.0 Task 13: PostToolUse:Bash test-fail auto-context bundle.
// Reads Claude Code PostToolUse JSON from stdin (tool_name, tool_input, tool_response).
// When tool_name == "Bash" and failure signatures detected in output, injects
// debug context block into additionalContext for the next turn.
int HookCommand::cmdPostToolUseBash() {
    std::string raw = readStdinAll();
    if (raw.empty()) return 0;

    std::string tool_name, tool_command, tool_output;
    try {
        json j = json::parse(raw);
        tool_name = j.value("tool_name", std::string{});
        if (j.contains("tool_input") && j["tool_input"].is_object())
            tool_command = j["tool_input"].value("command", std::string{});
        // tool_response may be a string or object with "output" field.
        if (j.contains("tool_response")) {
            auto& tr = j["tool_response"];
            if (tr.is_string())
                tool_output = tr.get<std::string>();
            else if (tr.is_object())
                tool_output = tr.value("output", std::string{});
        }
    } catch (...) { return 0; }

    // Only handle Bash tool â€” don't touch Read/Glob/Grep paths.
    if (tool_name != "Bash") return 0;

    std::string ctx = icmg::core::hooks::runPostToolUseTestFailContext(
        tool_command, tool_output);

    // v1.4.0 T5: record test outcome into approaches table (side-effect only).
    icmg::core::hooks::runPostToolUseTestOutcome(tool_command, tool_output, 0);
    std::string force = icmg::core::hooks::runPostToolForceCompress(tool_output);
    if (!force.empty()) { if (!ctx.empty()) ctx += force; else ctx = force; }

    if (ctx.empty()) return 0;

    json out;
    out["hookSpecificOutput"]["hookEventName"] = "PostToolUse";
    out["hookSpecificOutput"]["additionalContext"] = ctx;
    std::cout << icmg::core::safeDump(out) << "\n";
    return 0;
}


int HookCommand::cmdPreToolUseEditDisambig() {
    std::string raw = readStdinAll();
    std::string out = icmg::core::hooks::runPreToolUseEditDisambig(raw);
    if (!out.empty()) std::cout << out << "\n";
    return 0;
}

// v1.4.0 Task 3: PostToolUse:Edit/Write auto graph-update + memory draft.
// Reads Claude Code PostToolUse JSON from stdin (tool_name, tool_input).
// Silently writes a draft ContextNode for the edited file; returns 0.
int HookCommand::cmdPostToolUseEditAutoSync() {
    std::string raw = readStdinAll();
    icmg::core::hooks::runPostToolUseEditAutoSync(raw);
    return 0;
}

// v1.25.0 (W1/W3): PreToolUse:Write expander.
// Detects @@ICMG-RAW / @@ICMG-DIFF / @@ICMG-GLOSS / @@ICMG-TPL magic in
// tool_input.content. When found AND write-mode flag exists, expand via
// compress::expandCompressedWrite() and return updatedInput.content so
// Claude executes Write with the final on-disk bytes.
// Telemetry: every expand records to write_compressions table.
// Pass-through (no transform) when: flag absent, no magic header, parse fail.
int HookCommand::cmdPreToolUseWrite() {
    // Cheap gate: skip entirely if flag absent.
    fs::path flag = fs::path(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE")
                            : (std::getenv("HOME") ? std::getenv("HOME") : "."))
                  / ".icmg" / "write-mode.flag";
    if (!fs::exists(flag)) return 0;

    std::string raw = readStdinAll();
    if (raw.empty()) return 0;

    std::string file_path;
    std::string content;
    try {
        json j = json::parse(raw);
        if (j.contains("tool_input")) {
            auto& ti = j["tool_input"];
            if (ti.contains("file_path") && ti["file_path"].is_string())
                file_path = ti["file_path"].get<std::string>();
            if (ti.contains("content") && ti["content"].is_string())
                content = ti["content"].get<std::string>();
        }
    } catch (...) { return 0; }

    if (content.empty()) return 0;

    // Magic header probe — bail early when AI didn't emit compressed form.
    if (content.rfind("@@ICMG-", 0) != 0) return 0;

    auto result = icmg::compress::expandCompressedWrite(content, file_path);

    // Telemetry — best-effort, swallow errors.
    try {
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        db.run("INSERT INTO write_compressions(mode, base_path, bytes_compressed, "
               "bytes_expanded, ok, err) VALUES(?,?,?,?,?,?)",
               {result.mode, file_path,
                std::to_string(result.bytes_in),
                std::to_string(result.bytes_out),
                std::to_string(result.ok ? 1 : 0),
                result.error});
    } catch (...) {}

    if (!result.ok || result.mode == "raw") {
        // Pass-through — no updatedInput needed for raw; mark ok for parse fail.
        return 0;
    }

    json out;
    out["hookSpecificOutput"]["hookEventName"] = "PreToolUse";
    out["hookSpecificOutput"]["permissionDecision"] = "allow";
    out["hookSpecificOutput"]["updatedInput"]["file_path"] = file_path;
    out["hookSpecificOutput"]["updatedInput"]["content"] = result.content;
    out["hookSpecificOutput"]["additionalContext"] =
        "compressed-write: " + result.mode
        + " expanded " + std::to_string(result.bytes_in)
        + " -> " + std::to_string(result.bytes_out) + " bytes";
    std::cout << icmg::core::safeDump(out);
    return 0;
}

ICMG_REGISTER_COMMAND("hook", HookCommand);

} // namespace icmg::cli
