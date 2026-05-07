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
#include "../../imem/memory_store.hpp"
#include "../../imem/memory_node.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
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
            "  --dry-run       Show assembled prompt; do not call LLM\n"
            "  --no-store      Do not auto-store decision\n"
            "  --no-pack       Skip pack step (smaller prompt; for terse tasks)\n"
            "  --command CMD   Override agent.command (default: from config)\n"
            "  --timeout SEC   Hard timeout (default 120)\n\n"
            "Config (~/.icmg/config.json):\n"
            "  agent.command           shell cmd (default: \"claude --print\")\n"
            "  agent.system_prompt_path optional file prepended verbatim\n"
            "  agent.max_tokens        soft target (logged)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") { usage(); return 0; }

        bool dry_run = hasFlag(args, "--dry-run");
        bool no_store = hasFlag(args, "--no-store");
        bool no_pack  = hasFlag(args, "--no-pack");
        int timeout   = 120;
        try { timeout = std::stoi(flagValue(args, "--timeout", "120")); } catch (...) {}

        std::string task;
        for (auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            if (!task.empty()) task += " ";
            task += a;
        }
        if (task.empty()) { std::cerr << "icmg agent: requires <task>\n"; return 1; }

        auto& cfg = core::Config::instance();

        // Build prompt.
        std::ostringstream prompt;
        std::string sys_path = cfg.getString("agent.system_prompt_path", "");
        if (!sys_path.empty()) {
            std::ifstream f(sys_path);
            if (f) {
                std::ostringstream s; s << f.rdbuf();
                prompt << s.str() << "\n\n";
            }
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
#else
            pack_cmd = "icmg pack \"" + escape(task) + "\" --max-bytes 4096";
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
        std::string cmd = cmd_override.empty()
            ? cfg.getString("agent.command", "claude --print")
            : cmd_override;
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
        auto t0 = std::chrono::steady_clock::now();
        auto res = core::safeExecShell(full_cmd, false, timeout * 1000);
        auto t1 = std::chrono::steady_clock::now();
        std::remove(tmp.c_str());

        if (res.exit_code != 0) {
            std::cerr << "icmg agent: LLM exited " << res.exit_code << "\n"
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
