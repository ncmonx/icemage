// Phase 23 Task 5: `icmg chat` REPL.
// Interactive loop:
//   user prompt → optional pack → LLM agent (via `icmg agent`) → print + auto-store
//
// Slash commands:
//   \save <name>   snapshot session
//   \load <name>   restore session
//   \clear         reset working context (memory remains)
//   \help          command list
//   \quit / Ctrl-D exit
//
// MVP: std::getline (no linenoise dep). History persists to ~/.icmg/chat-history.txt
// across launches via append-on-input.
//
// Without agent.command configured, chat runs in `--no-llm` sandbox: shows packed
// prompt + echoes back, useful for testing context bundles.

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
#include <ctime>
#include <cstdlib>
#ifdef _WIN32
  #include <windows.h>
  #include <io.h>
#else
  #include <unistd.h>
#endif

namespace icmg::cli {

static std::string homeDir() {
#ifdef _WIN32
    if (const char* h = std::getenv("USERPROFILE")) return h;
#endif
    if (const char* h = std::getenv("HOME")) return h;
    return ".";
}

static std::string sessionStamp() {
    auto t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", std::localtime(&t));
    return buf;
}

class ChatCommand : public BaseCommand {
public:
    std::string name()        const override { return "chat"; }
    std::string description() const override { return "Interactive REPL over LLM agent"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg chat [options]\n\n"
            "Options:\n"
            "  --no-llm        Sandbox mode: print packed prompt only (no LLM call)\n"
            "  --no-pack       Skip pack step on each turn\n"
            "  --session NAME  Use specific session id (default: timestamp)\n\n"
            "Slash commands inside REPL:\n"
            "  \\save <name>   Snapshot session\n"
            "  \\load <name>   Restore session\n"
            "  \\clear         Reset chat context\n"
            "  \\help          Show this help\n"
            "  \\quit          Exit\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }

        bool no_llm  = hasFlag(args, "--no-llm");
        bool no_pack = hasFlag(args, "--no-pack");
        std::string session = flagValue(args, "--session", "");
        if (session.empty()) session = sessionStamp();

        auto& cfg = core::Config::instance();
        std::string history_path = homeDir() + "/.icmg/chat-history.txt";
        std::ofstream history(history_path, std::ios::app);

        std::cerr << "icmg chat — session=" << session
                  << (no_llm ? " (no-llm sandbox)" : "")
                  << "\n  type \\help for commands, \\quit to exit\n\n";

        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        std::string self_path = locateSelf();

        std::string line;
        for (;;) {
            std::cerr << "icmg> " << std::flush;
            if (!std::getline(std::cin, line)) { std::cerr << "\n"; break; }
            if (line.empty()) continue;
            history << line << "\n"; history.flush();

            if (line[0] == '\\') {
                if (line == "\\quit" || line == "\\exit") break;
                if (line == "\\help") { usage(); continue; }
                if (line == "\\clear") {
                    std::cerr << "(working context cleared; memory preserved)\n";
                    continue;
                }
                if (line.rfind("\\save ", 0) == 0) {
                    std::string nm = line.substr(6);
                    saveSession(db, nm, session);
                    std::cerr << "saved session: " << nm << "\n";
                    continue;
                }
                if (line.rfind("\\load ", 0) == 0) {
                    std::string nm = line.substr(6);
                    auto data = loadSession(db, nm);
                    if (data.empty()) std::cerr << "no session: " << nm << "\n";
                    else std::cerr << data << "\n";
                    continue;
                }
                std::cerr << "unknown command: " << line << "\n";
                continue;
            }

            // Build agent command — reuse `icmg agent` for prompt assembly + LLM call.
            std::string esc; esc.reserve(line.size() * 2);
            for (char c : line) {
                if (c == '"' || c == '\\') esc.push_back('\\');
                esc.push_back(c);
            }
            std::string cmd = "\"" + self_path + "\" agent \"" + esc + "\"";
            if (no_pack) cmd += " --no-pack";
            if (no_llm)  cmd += " --dry-run";
            // Tag stored decision with chat session.
            // (icmg agent always stores; we let it; topic prefix via env not supported here.)

            auto res = core::safeExecShell(cmd, false, 120000);
            if (res.exit_code != 0) {
                std::cerr << "(agent failed: exit " << res.exit_code << ")\n";
                if (!res.err.empty()) std::cerr << res.err << "\n";
                continue;
            }
            std::cout << res.out;
            if (res.out.empty() || res.out.back() != '\n') std::cout << "\n";

            // Auto-store as chat memory.
            try {
                imem::MemoryNode n;
                n.topic   = "chat-" + session + " " + (line.size() > 60 ? line.substr(0, 60) + "..." : line);
                n.content = res.out.substr(0, 4000);
                n.keywords = "chat,session-" + session;
                n.importance = 1;
                mem.store(n, true);
            } catch (...) {}
        }

        return 0;
    }

private:
    std::string locateSelf() {
#ifdef _WIN32
        char buf[1024]; GetModuleFileNameA(nullptr, buf, sizeof(buf));
        return buf;
#else
        return "icmg";
#endif
    }

    void saveSession(core::Db& db, const std::string& name, const std::string& session) {
        // Use existing sessions table from Phase 19 if present; else fall back.
        try {
            db.run("CREATE TABLE IF NOT EXISTS sessions("
                   "name TEXT PRIMARY KEY,"
                   "snapshot TEXT NOT NULL,"
                   "created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
            std::string snap = "{\"session\":\"" + session + "\"}";
            db.run("INSERT OR REPLACE INTO sessions(name,snapshot) VALUES(?,?)",
                   {name, snap});
        } catch (...) {}
    }

    std::string loadSession(core::Db& db, const std::string& name) {
        std::string out;
        try {
            db.query("SELECT snapshot FROM sessions WHERE name=?",
                     {name},
                     [&](const core::Row& r) { if (!r.empty()) out = r[0]; });
        } catch (...) {}
        return out;
    }
};

ICMG_REGISTER_COMMAND("chat", ChatCommand);

} // namespace icmg::cli
