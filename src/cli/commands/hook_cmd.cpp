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
        if (event == "userprompt") return cmdUserPrompt();
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
        try {
            auto& cfg = core::Config::instance();
            core::Db db(cfg.projectDbPath("."));
            imem::MemoryStore mem(db);
            auto results = mem.recall(prompt, 3, false);
            if (!results.empty()) {
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
};

ICMG_REGISTER_COMMAND("hook", HookCommand);

} // namespace icmg::cli
