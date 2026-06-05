// Phase 23 Task 3: `icmg agent <task>` — LLM proxy.
// Flow:
//   1. Build prompt = system_prompt + `icmg pack <task>` output + task statement.
//   2. Pass prompt to user's configured LLM CLI (agent.command in config).
//      Default: "claude --print" (Claude Code one-shot).
//   3. Capture stdout. Auto-store decision via memory_nodes (topic="decisions-agent").
//
// Hard token cap (agent.max_tokens, default 2000) — note: cap not enforced
// here (depends on LLM CLI flags). Logged into tool_invocations.
//
// Security: agent.command is shell — user-supplied. Never reads secrets.json.
// Honors agent.api_key_env (env var name) — child process inherits.
//
// Dry-run mode: --dry-run prints assembled prompt and exits without LLM call.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/persona_loader.hpp"  // v1.42.0 persona prefix
#include "../agent_persona_policy.hpp"   // sub-agent persona policy
#include "../agent_task.hpp"             // flag/value-aware task assembly
#include "../agent_model.hpp"            // token routing (--light / --model)
#include "../agent_complexity.hpp"       // auto-route mechanical tasks to cheap model
#include "../../imem/memory_store.hpp"
#include "../../imem/memory_node.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <cstdio>
#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <cstdlib>
#endif

namespace icmg::cli {

class AgentCommand : public BaseCommand {
public:
    std::string name()        const override { return "agent"; }
    std::string description() const override { return "LLM agent proxy (pack + configured CLI)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg agent <task description...> [options]\n\n"
            "Options:\n"
            "  --exec          Run as a real sub-agent: spawned CLI gets edit/write/bash\n"
            "                  tools + auto-accept, EXECUTES the task (not just advises).\n"
            "                  Requires ICMG_AGENT_EXEC=1 (arbitrary local automation gate).\n"
            "  --dry-run       Show assembled prompt; do not call LLM\n"
            "  --no-store      Do not auto-store decision\n"
            "  --no-pack       Skip pack step (smaller prompt; for terse tasks)\n"
            "  --command CMD   Override the command (default: from config)\n"
            "  --light         Token-saving: cheap model + skip pack/persona\n"
            "  --model NAME    Override the model (e.g. claude-haiku-4-5)\n"
            "  --timeout SEC   Hard timeout (default 120; 600 with --exec)\n\n"
            "Config (~/.icmg/config.json):\n"
            "  agent.command           advisory CLI (default: \"claude --print\")\n"
            "  agent.exec_command      agentic CLI for --exec (default: claude --print\n"
            "                          --permission-mode acceptEdits --allowedTools ...)\n"
            "  agent.system_prompt_path optional file prepended verbatim\n"
            "  agent.max_tokens        soft target (logged)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") { usage(); return 0; }

        bool dry_run = hasFlag(args, "--dry-run");
        bool no_store = hasFlag(args, "--no-store");
        bool no_pack  = hasFlag(args, "--no-pack");
        // v1.79: --exec turns the proxy into a real sub-agent — the spawned CLI
        // gets write/edit/bash tools + auto-accept so it executes the task
        // (edits files, runs build/tests) instead of just printing advice.
        bool exec     = hasFlag(args, "--exec");
        // Token routing: --light -> cheap model + lean prompt (skip pack/persona);
        // --model X overrides the model explicitly. See agent_model.hpp.
        bool light    = hasFlag(args, "--light");
        std::string model_override = flagValue(args, "--model", "");
        if (light) no_pack = true;
        // agentic runs take longer than one-shot Q&A; default 600s in exec mode.
        int timeout   = exec ? 600 : 120;
        try { timeout = std::stoi(flagValue(args, "--timeout", std::to_string(timeout))); } catch (...) {}

        std::string task = assembleTask(args);
        if (task.empty()) { std::cerr << "icmg agent: requires <task>\n"; return 1; }

        // Auto-route: clearly-mechanical tasks default to the cheap model (no
        // manual --light needed). Conservative — only when confident.
        bool auto_light = !light && model_override.empty() && isLightweightTask(task);
        if (auto_light)
            std::cerr << "[icmg agent] auto-routed to light model (mechanical task)\n";

        auto& cfg = core::Config::instance();

        // Build prompt.
        std::ostringstream prompt;
        // Persona policy: coding sub-agents (--exec) stay clean engineers (never persona);
        // advisory is opt-in (agent.use_persona, default off); ICMG_NO_PERSONA=1 forces off.
        bool no_persona_env = std::getenv("ICMG_NO_PERSONA") != nullptr || light;
        if (agentUsePersona(exec, no_persona_env, cfg.getBool("agent.use_persona", false))) {
            std::string persona_prefix = icmg::core::buildPersonaPrefix();
            if (!persona_prefix.empty()) {
                prompt << persona_prefix;
            }
        }
        std::string sys_path = cfg.getString("agent.system_prompt_path", "");
        if (!sys_path.empty()) {
            std::ifstream f(sys_path);
            if (f) {
                std::ostringstream s; s << f.rdbuf();
                prompt << s.str() << "\n\n";
            }
        } else if (exec) {
            // v1.79: agentic sub-agent system prompt — the CLI has write/edit/bash
            // tools and must EXECUTE the task end-to-end, not just advise.
            prompt << "You are an autonomous engineering sub-agent with file-edit and "
                      "shell access. EXECUTE the task fully: make the edits, run the "
                      "build/tests, and fix failures until green. Do not ask for "
                      "confirmation — you are running headless.\n\n"
                      "## Rules\n"
                      "- Use `icmg context <file>`, `icmg graph symbol <name>`, "
                      "`icmg recall \"<q>\"` instead of raw Read/Grep when possible.\n"
                      "- For 2+ independent shell steps: use `icmg parallel --task ... --task ...`.\n"
                      "- Build via `powershell -File build.ps1` (MSVC) — never raw cmake.\n"
                      "- Follow TDD: failing test first, then implement, then verify green.\n"
                      "- When done you MUST end with a structured report (never skip it):\n"
                      "  ## FINAL REPORT\n"
                      "  - Summary: what changed and why (1-3 sentences).\n"
                      "  - Files: paste the output of `git diff --stat` for your changes.\n"
                      "  - Verification: each build/test command you ran and its key result\n"
                      "    line (e.g. pass/fail counts, error text). Quote actual output.\n"
                      "  - Deviations: anything done differently from the task, or blockers\n"
                      "    hit, or 'none'.\n"
                      "- After a fix, store via `icmg known-issue add`; after a decision, "
                      "`icmg store --topic decisions-...`.\n\n";
        } else {
            prompt << "You are an engineering assistant. Be concise. "
                      "Give a decision sentence followed by minimal code or steps.\n\n"
                      "## Tooling rules (icmg-aware)\n"
                      "- Use `icmg context <file>`, `icmg graph symbol <name>`, "
                      "`icmg recall \"<q>\"` instead of raw Read/Grep when possible.\n"
                      "- For 2+ independent shell steps: use `icmg parallel --task ... --task ...`. "
                      "Sequential runs are a bug.\n"
                      "- After fixing a bug, store via `icmg known-issue add`. "
                      "After a non-trivial decision, store via `icmg store --topic decisions-...`.\n\n";
        }

        if (!no_pack) {
            std::string pack_cmd;
#ifdef _WIN32
            // Locate icmg.exe alongside this binary.
            char buf[1024]; GetModuleFileNameA(nullptr, buf, sizeof(buf));
            pack_cmd = std::string("\"") + buf + "\" pack \"" + escape(task) + "\" --max-bytes 4096";
            if (hasFlag(args, "--no-think"))    pack_cmd += " --no-think";
            if (hasFlag(args, "--concise"))     pack_cmd += " --concise";
            if (hasFlag(args, "--sayless"))     pack_cmd += " --sayless";
            if (hasFlag(args, "--auto-think"))  pack_cmd += " --auto-think";
            if (hasFlag(args, "--cache-prefix")) pack_cmd += " --cache-prefix";
#else
            pack_cmd = "icmg pack \"" + escape(task) + "\" --max-bytes 4096";
            if (hasFlag(args, "--no-think"))    pack_cmd += " --no-think";
            if (hasFlag(args, "--concise"))     pack_cmd += " --concise";
            if (hasFlag(args, "--sayless"))     pack_cmd += " --sayless";
            if (hasFlag(args, "--auto-think"))  pack_cmd += " --auto-think";
            if (hasFlag(args, "--cache-prefix")) pack_cmd += " --cache-prefix";
#endif
            auto pack_res = core::safeExecShell(pack_cmd, false, 30000);
            if (pack_res.exit_code == 0 && !pack_res.out.empty()) {
                prompt << pack_res.out << "\n\n";
            }
        }

        prompt << "## Task\n" << task << "\n";

        std::string assembled = prompt.str();

        if (dry_run) {
            std::cout << assembled;
            return 0;
        }

        // Resolve command.
        std::string cmd_override = flagValue(args, "--command", "");
        // v1.79: exec mode picks the agentic command (write+bash tools, auto-accept
        // edits) so the spawned CLI behaves like a real sub-agent. Default mirrors
        // Claude Code headless agentic invocation; override via agent.exec_command.
        std::string default_cmd = exec
            ? cfg.getString("agent.exec_command",
                  "claude --print --permission-mode acceptEdits "
                  "--allowedTools \"Edit,Write,Read,Bash,Glob,Grep\"")
            : cfg.getString("agent.command", "claude --print");
        std::string cmd = cmd_override.empty() ? default_cmd : cmd_override;
        cmd = applyAgentModel(cmd, light || auto_light, model_override);  // token routing (+ auto)

        // v1.79 SECURITY: --exec grants the spawned CLI write + shell with
        // auto-accept (arbitrary headless command execution). Gate behind an
        // explicit env ack so it can never fire accidentally from another tool,
        // and always echo the resolved command first. dry-run is exempt (no spawn).
        if (exec && !dry_run) {
            const char* ack = std::getenv("ICMG_AGENT_EXEC");
            if (!(ack && ack[0] == '1')) {
                std::cerr << "icmg agent --exec: agentic mode lets the spawned CLI edit files "
                             "and run shell commands with auto-accept.\n"
                          << "  Resolved command: " << cmd << "\n"
                          << "  This is arbitrary local automation. To proceed, set "
                             "ICMG_AGENT_EXEC=1 in your environment.\n";
                return 2;
            }
            std::cerr << "[icmg agent --exec] agentic run (auto-accept edits + shell): "
                      << cmd << "\n";
        }
        if (cmd.empty()) {
            std::cerr << "icmg agent: agent.command not configured. "
                         "Set in ~/.icmg/config.json or pass --command.\n";
            return 2;
        }

        // Pipe prompt to LLM via stdin. safeExecShell doesn't support stdin —
        // workaround: write to temp file + use shell here-string redirect.
        auto tmp = makeTempFile();
        {
            std::ofstream f(tmp); f << assembled;
        }
        std::string full_cmd;
#ifdef _WIN32
        full_cmd = cmd + " < \"" + tmp + "\"";
#else
        full_cmd = cmd + " < '" + tmp + "'";
#endif
        // Phase 68 T2: retry/backoff on transient errors.
        // exit code 429 (rate-limit) / 503 (overload) / 504 (timeout) → retry
        // up to 3x with exponential backoff (2s, 4s, 8s). Disable via --no-retry.
        bool no_retry = hasFlag(args, "--no-retry");
        int max_retries = no_retry ? 0 : 3;
        auto t0 = std::chrono::steady_clock::now();
        core::ExecResult res;
        int attempt = 0;
        while (true) {
            res = core::safeExecShell(full_cmd, false, timeout * 1000);
            ++attempt;
            bool transient = (res.exit_code == 429 || res.exit_code == 503 ||
                              res.exit_code == 504);
            // Some CLIs lift HTTP status codes to stderr; cheap detect.
            if (!transient && (res.err.find("rate_limit") != std::string::npos ||
                                res.err.find("overloaded") != std::string::npos ||
                                res.err.find("Too Many Requests") != std::string::npos)) {
                transient = true;
            }
            if (!transient || attempt > max_retries) break;
            int wait_s = 1 << attempt;  // 2, 4, 8
            std::cerr << "[icmg agent] transient error (exit " << res.exit_code
                      << "), retry " << attempt << "/" << max_retries
                      << " in " << wait_s << "s\n";
            std::this_thread::sleep_for(std::chrono::seconds(wait_s));
        }
        auto t1 = std::chrono::steady_clock::now();
        std::remove(tmp.c_str());

        if (res.exit_code != 0) {
            std::cerr << "icmg agent: LLM exited " << res.exit_code
                      << " after " << attempt << " attempt(s)\n"
                      << res.err;
            return res.exit_code;
        }

        std::cout << res.out;

        // Auto-store decision.
        if (!no_store) {
            try {
                core::Db db(cfg.projectDbPath("."));
                imem::MemoryStore mem(db);
                imem::MemoryNode n;
                n.topic   = "decisions-agent " + truncate(task, 60);
                n.content = truncate(res.out, 4000);
                n.keywords = "agent,decision";
                n.importance = 2;
                try { mem.store(n, true); } catch (...) {}
            } catch (...) {}
        }

        // Log invocation.
        int64_t dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        try {
            core::Db db(cfg.projectDbPath("."));
            db.run("INSERT INTO tool_invocations(tool_name,command,raw_bytes,filtered_bytes,"
                   "est_tokens_in,est_tokens_out,saved_tokens) VALUES(?,?,?,?,?,?,?)",
                   {"agent", "agent " + truncate(task, 100),
                    std::to_string(assembled.size()),
                    std::to_string(res.out.size()),
                    std::to_string(assembled.size() / 4),
                    std::to_string(res.out.size() / 4),
                    "0"});
        } catch (...) {}
        (void)dur_ms;

        return 0;
    }

private:
    static std::string escape(const std::string& s) {
        std::string out; out.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }
    static std::string truncate(const std::string& s, size_t n) {
        return s.size() <= n ? s : s.substr(0, n) + "...";
    }
    static std::string makeTempFile() {
#ifdef _WIN32
        char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp);
        char name[MAX_PATH]; GetTempFileNameA(tmp, "icmg", 0, name);
        return name;
#else
        char tpl[] = "/tmp/icmg_agentXXXXXX";
        int fd = mkstemp(tpl); if (fd >= 0) close(fd);
        return tpl;
#endif
    }
};

ICMG_REGISTER_COMMAND("agent", AgentCommand);

} // namespace icmg::cli
