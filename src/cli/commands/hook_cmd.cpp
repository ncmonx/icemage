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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

        // 2. Memory recall — top 3 hits (in-process via MemoryStore::recall).
        int local_hits = 0;
        try {
            auto& cfg = core::Config::instance();
            core::Db db(cfg.projectDbPath("."));
            imem::MemoryStore mem(db);
            auto results = mem.recall(prompt, 3, false);
            if (!results.empty()) {
                local_hits = (int)results.size();
                msg << "icmg memory hits (proactively surfaced):\n";
                for (auto& m : results) {
                    std::string topic = m.topic;
                    if (topic.size() > 80) topic = topic.substr(0, 77) + "...";
                    msg << "  [" << std::fixed
                        << static_cast<int>(m.score) << "] "
                        << topic << "\n";
                }
                msg << "\n";
            }
        } catch (...) {}

        // 2b. Cross-project recall fallback when local hits sparse.
        if (local_hits < 2 && std::getenv("ICMG_CROSS_PROJECT") == nullptr) {
            // Iterate registered projects via global registry DB.
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
                    // Skip current project.
                    if (ppath == cwd) continue;
                    std::string pdb = ppath + "/.icmg/data.db";
                    if (!fs::exists(pdb)) continue;
                    try {
                        core::Db pd(pdb);
                        imem::MemoryStore pm(pd);
                        auto pres = pm.recall(prompt, 2, false);
                        for (auto& m : pres) {
                            if (xhits >= 3) break;
                            std::string topic = m.topic;
                            if (topic.size() > 70) topic = topic.substr(0, 67) + "...";
                            xmsg << "  [" << pname << "][" << (int)m.score
                                 << "] " << topic << "\n";
                            ++xhits;
                        }
                    } catch (...) {}
                    if (xhits >= 3) break;
                }
                if (xhits > 0) {
                    msg << "cross-project recall (other projects):\n"
                        << xmsg.str() << "\n";
                }
            } catch (...) {}
        }

        // 2c. Path-context — detect file path mentions, emit graph context.
        {
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
                } catch (...) {}
            }
        }

        // 3. Compress suggestion — large prompt heuristic.
        size_t sz = prompt.size();
        if (sz > 4000) {
            msg << "(Large prompt " << sz
                << "B — pipe big paste through `icmg compress` next time.)\n";
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
