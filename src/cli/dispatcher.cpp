#include "dispatcher.hpp"
#include "core/version.hpp"
#include "base_command.hpp"
#include "../core/registry.hpp"
#include "../core/config.hpp"
#include "../core/global_db.hpp"
#include "../core/project_context.hpp"
#include "../server/rpc_protocol.hpp"   // v1.57 S1: daemon fast-path
#include "../llm/warm_pipe.hpp"         // v1.57 S1: PipeClient
#include "../core/update_lock.hpp"      // v1.78.4: updating.lock startup check
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>
#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <signal.h>
#endif

// Pull in all registered commands (each file registers via ICMG_REGISTER_COMMAND)
// The linker needs to see these TUs to trigger static-init registration.
// We rely on GLOB_RECURSE in CMakeLists.txt to include all src/*.cpp files.

namespace icmg::cli {

// Inline stub commands for phase-01 skeleton.
// Real implementations registered via ICMG_REGISTER_COMMAND in later phases.

struct StubCommand : BaseCommand {
    std::string name_;
    std::string desc_;
    StubCommand(std::string n, std::string d)
        : name_(std::move(n)), desc_(std::move(d)) {}
    std::string name()        const override { return name_; }
    std::string description() const override { return desc_; }
    int run(const std::vector<std::string>&) override {
        std::cout << name_ << ": not yet implemented (planned)\n";
        return 0;
    }
};

// Command table: name → description
static const std::vector<std::pair<std::string,std::string>> CMDS = {
    {"store",    "Store a memory node"},
    {"recall",   "Recall memory nodes by query"},
    {"memory",   "Memory management (list, show, search, stats, purge)"},
    {"graph",    "Manage knowledge graph (scan, query, visualize)"},
    {"zone",     "Zone management (partition graph + memory by subsystem)"},
    {"run",      "Run command through Tkil filter"},
    {"sp",       "Stored procedure management"},
    {"abbr",     "Abbreviation management"},
    {"rule",     "Per-folder rule management"},
    {"data",     "Structured data (model/view/behavior/schema)"},
    {"project",  "Multi-project registry"},
    {"cmd",      "Command frequency tracking"},
    {"stats",    "Show usage statistics"},
    {"import",   "Import from ICM/Graphify/JSON/CSV"},
    {"export",   "Export data"},
    {"doctor",   "Health check (DB integrity, schema version, config)"},
    {"known-issue", "Recurring error registry (add/match/list/stats)"},
    {"verify",   "Run + record verification commands (audit trail + gate)"},
    {"phase",    "Phase lifecycle (start/verify/ship)"},
    {"design",   "Design doc registry (register/approve/check)"},
    {"wflog",    "Queryable session log (save/search/recent/show)"},
    {"context",  "File context bundle (graph + symbols + memory)"},
    {"pack",     "Task-context bundle (recall + files + rules)"},
    {"diff-summary", "Symbol-aware git diff summary"},
    {"explain",  "Match error against past resolutions"},
    {"session",  "Snapshot active task context (save/restore/list)"},
    {"summarize","Heuristic file outline (avoid full Read on large files)"},
    {"budget",   "Token-budget tracker (per-tool savings + hot spots)"},
    {"parallel", "Run multiple commands concurrently (subprocess fan-out)"},
    {"filter",   "Apply Tkil filter to stdin (pipe-style)"},
    {"embed",    "Build/refresh embeddings (semantic recall index)"},
    {"agent",    "LLM agent proxy (uses pack + configured CLI)"},
    {"chat",     "Interactive REPL over LLM agent"},
    {"ls",       "Token-friendly directory listing"},
    {"init",     "Bootstrap project (hooks + AGENTS.md routing)"},
    {"memoir",   "Long-form narrative memory (essays, post-mortems)"},
    {"wiki",     "Generate Markdown + HTML wiki from graph"},
    {"parity",   "Symbol parity check between two files"},
    {"template", "Manifest-driven scaffold from a reference file"},
    {"wake-up",  "Session-start briefing (decisions, fixes, phases)"},
    {"discover", "Scan transcripts for missed icmg-run opportunities"},
    {"update",   "Self-update from github releases (--check/--apply/--rollback)"},
    {"feedback", "Record recall quality feedback for reranking"},
    {"config",   "Read/write ~/.icmg/config.json"},
    {"completions", "Emit shell completion script (bash/zsh/powershell)"},
    {"lint-style",  "Text-pattern style/UI consistency lint"},
    {"index",    "Unified maintenance pipeline (scan + embed + consolidate + patterns + decay)"},
    {"review",   "PR pre-flight gate: parity + lint-style on git-diff"},
    {"pr-summary", "Generate markdown PR description from git + verifications"},
    {"ask",      "Natural-language meta-router: cosine-match question to commands"},
    {"serve",    "Embedded HTTP dashboard (read-only) — http://127.0.0.1:8080/"},
    {"compress", "Semantic prompt compression with reversible glossary (Phase 39)"},
    {"expand",   "Reverse `icmg compress` output via glossary"},
    {"savings",  "Token-savings dashboard (with/without comparison + HTML)"},
    {"shrink",   "Auto-detect content type + intelligent shrink (grep/log/SQL/JSON/generic)"},
    {"whats-new", "Show release notes (current or --since X) — call after icmg update"},
    {"fetch",    "Download URL with content-aware reduction (HTML/JSON/PDF/binary) + cache"},
    {"batch",    "Emit Anthropic Batch API spec (50% bulk discount)"},
    {"sync",     "Team sync via git-tracked JSONL (init/push/pull/merge/status)"},
    {"sayless",  "Toggle sayless directive auto-inject (on/off/status/level)"},
    {"health",   "Single sanity check (DB / hooks / version / sidecars / telemetry)"},
    {"strict",   "Toggle hook-level rule enforcement (on/off/status)"},
    {"fail",     "Anti-pattern memory: store/recall failed approaches"},
    {"lint-claudemd", "Detect stale file paths / symbol refs in CLAUDE.md / AGENTS.md"},
    {"test-select", "Pick test files affected by current diff"},
    {"receipt",  "Itemized token receipts from recent pack/context/recall calls"},
    {"distill",  "Auto-extract decisions/facts/anti-patterns from response text into memory"},
    {"correction", "Track diffs between AI-emitted code and user fixes"},
    {"entropy",  "File-edit entropy from git history (hot files = bad pack candidates)"},
    {"tool-budget", "Per-turn tool-call gate (prevents runaway loops)"},
    {"shorten",  "Heuristic prompt rewriter — strip filler/politeness/redundancy"},
    {"context-budget", "Real Claude session token usage from transcript (covers ALL sources)"},
    {"compliance", "Track + surface sayless thinking-phase directive violations"},
    {"cron",       "Install/uninstall weekly memory-hygiene scheduler (Win schtasks / POSIX cron)"},
    {"cross-recall", "Cross-project memory recall — find tasks solved in other projects"},
};

Dispatcher::Dispatcher() {}

// v1.21.9: sweep `.old-<PID>` files left behind by previous `update --apply`
// runs. v1.21.x's rename-aside strategy puts old DLLs / icmg.exe.bak under
// names like `libwinpthread-1.dll.old-9100` when the holding process can't
// release the handle in time. Once that PID dies, the file is finally
// deletable — sweep on every icmg startup so the bin dir doesn't accrete.
// Silent on failures (permission, file still in use under a fresh PID).
static void sweepStaleOldFiles() {
    namespace fs = std::filesystem;
    try {
        char buf[1024];
#ifdef _WIN32
        GetModuleFileNameA(nullptr, buf, sizeof(buf));
#else
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0) return;
        buf[n] = '\0';
#endif
        fs::path self = buf;
        fs::path dir = self.parent_path();
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            std::string name = entry.path().filename().string();
            // Pattern: `*.old-<digits>`
            auto dot = name.rfind(".old-");
            if (dot == std::string::npos) continue;
            std::string pid_s = name.substr(dot + 5);
            if (pid_s.empty()) continue;
            bool all_digits = true;
            for (char c : pid_s) if (!std::isdigit((unsigned char)c)) { all_digits = false; break; }
            if (!all_digits) continue;
            // Check PID alive.
            long pid = 0;
            try { pid = std::stol(pid_s); } catch (...) { continue; }
            bool alive = false;
#ifdef _WIN32
            if (pid > 0) {
                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                       FALSE, (DWORD)pid);
                if (h) {
                    DWORD ex = 0;
                    if (GetExitCodeProcess(h, &ex) && ex == STILL_ACTIVE) alive = true;
                    CloseHandle(h);
                }
            }
#else
            if (pid > 0) alive = (kill((pid_t)pid, 0) == 0);
#endif
            if (alive) continue;  // owner still around — wait
            std::error_code rm_ec;
            fs::remove(entry.path(), rm_ec);
            // Silent — best-effort.
        }
    } catch (...) {}
}

// Phase 46 T3: best-effort pending-upgrade swap on startup. Silent if nothing.
static void applyPendingUpgrade() {
    namespace fs = std::filesystem;
    fs::path self;
#ifdef _WIN32
    char buf[1024]; GetModuleFileNameA(nullptr, buf, sizeof(buf));
    self = buf;
#else
    try { self = fs::canonical("/proc/self/exe"); } catch (...) { return; }
#endif
    fs::path pending = self; pending += ".pending-restart";
    if (!fs::exists(pending)) return;

    // Read flag → target path of staged binary.
    std::string tag, staged_path;
    {
        std::ifstream pf(pending);
        if (!pf) return;
        std::getline(pf, tag);
        std::getline(pf, staged_path);
    }
    if (staged_path.empty() || !fs::exists(staged_path)) {
        fs::remove(pending);
        return;
    }
    // Self may still be locked here (we ARE running). Best chance: cmd.exe spawn
    // a detached helper that waits 2s then renames. On Unix, atomic rename works.
    std::error_code ec;
    fs::path bak = self; bak += ".bak";
    fs::remove(bak, ec);
    fs::rename(self, bak, ec);
    if (ec) {
        // Still locked — leave pending in place silently, retry next session.
        return;
    }
    fs::rename(staged_path, self, ec);
    if (ec) {
        // Restore .bak, leave pending.
        fs::rename(bak, self, ec);
        return;
    }
    fs::remove(pending);
    std::cerr << "[icmg] pending upgrade applied: " << tag << "\n";
}

int Dispatcher::run(const std::vector<std::string>& args) {
    applyPendingUpgrade();
    sweepStaleOldFiles();  // v1.21.9: clean up `.old-<PID>` from prior updates
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        printHelp();
        return 0;
    }

    // Parse --project <name> from args (can appear anywhere before command)
    std::string project_flag;
    std::vector<std::string> cleaned;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--project" && i + 1 < args.size()) {
            project_flag = args[++i];
        } else {
            cleaned.push_back(args[i]);
        }
    }

    // Resolve project context + set override on Config
    if (!project_flag.empty()) {
        try {
            auto ctx = core::ProjectContext::resolve(project_flag);
            // A2: cross-project warning
            std::cerr << "⚠  Cross-project read: " << ctx.name()
                      << " (" << ctx.rootPath() << ")\n"
                      << "   No authentication — any local user can read this data.\n";
            core::Config::instance().setProjectDbOverride(ctx.dbPath());
        } catch (const std::exception& e) {
            std::cerr << "icmg: " << e.what() << "\n";
            return 1;
        }
    }

    std::string cmd = cleaned.empty() ? "" : cleaned[0];
    if (cmd.empty()) { printHelp(); return 0; }

    // v1.78.4: bail if a binary upgrade swap is in progress. Without this
    // check, hooks + Claude Code kept spawning new icmg.exe processes that
    // grabbed the exe file handle, causing fs::rename(self, .bak) in
    // update --apply to fail with ERROR_SHARING_VIOLATION.
    // Allow "update" through so --rollback works during the lock window.
    if (cmd != "update" && icmg::core::isUpdatingLockFresh()) {
        std::cerr << "icmg: upgrade in progress (~/.icmg/updating.lock) — retry in a moment.\n"
                  << "  To force rollback: icmg update --rollback\n";
        return 1;
    }
    std::vector<std::string> rest(cleaned.begin() + 1, cleaned.end());

    // v1.57 S1: optional daemon fast-path. When ICMG_USE_SERVER=1 and a
    // daemon answers, forward the command over the pipe so per-call
    // cold-start (~30 ms: Config load + DB open) is skipped. Falls through
    // to in-process dispatch on any failure. 'server' is never forwarded
    // (it controls the daemon); --project is not forwarded (daemon holds
    // its own cwd context).
    if (cmd != "server" && project_flag.empty()) {
        if (const char* e = std::getenv("ICMG_USE_SERVER"); e && *e && *e != '0') {
            auto client = icmg::llm::PipeClient::connect(
                "icmg-server", std::chrono::milliseconds(300));
            if (client.has_value()) {
                icmg::server::RpcRequest req;
                req.cmd  = cmd;
                req.args = rest;
                if (const char* sid = std::getenv("CLAUDE_SESSION_ID"); sid && *sid)
                    req.session_id = sid;
                std::string resp =
                    client->sendRequest(icmg::server::serializeRequest(req));
                if (!resp.empty()) {
                    auto parsed = icmg::server::parseResponse(resp);
                    if (parsed.has_value() && parsed->ok) {
                        std::cout << parsed->out;
                        return parsed->exit_code;
                    }
                }
                // fall through to in-process dispatch on any failure
            }
        }
    }

    // v1.20.0 (bugfix): RAII guard — clear project-DB override on any return path
    // so the singleton Config doesn't leak `--project X` state across CLI
    // invocations in long-lived service / exec_server processes (cross-project
    // data appearing in active project's context query).
    struct OverrideGuard {
        ~OverrideGuard() { core::Config::instance().clearProjectDbOverride(); }
    } _override_guard;

    // Check registry first (real implementations from later phases)
    auto& reg = icmg::core::Registry<icmg::cli::BaseCommand>::instance();
    if (reg.has(cmd)) {
        auto handler = reg.create(cmd);
        return handler->run(rest);
    }

    // Try compound command: "graph scan" → look up "graph-scan" in registry
    if (!rest.empty()) {
        std::string compound = cmd + "-" + rest[0];
        if (reg.has(compound)) {
            auto handler = reg.create(compound);
            return handler->run(std::vector<std::string>(rest.begin() + 1, rest.end()));
        }
    }

    // Fall back to stubs
    for (auto& [name, desc] : CMDS) {
        if (name == cmd) {
            StubCommand stub(name, desc);
            return stub.run(rest);
        }
    }

    std::cerr << "icmg: unknown command: " << cmd << "\n";
    std::cerr << "Run 'icmg --help' for usage.\n";
    return 1;
}

void Dispatcher::printHelp() const {
    std::cout << "icmg " << icmg::core::ICMG_VERSION
              << " — unified memory, graph, and Tkil tool\n\n"
              << "Usage: icmg <command> [options]\n\n"
              << "Commands:\n";
    for (auto& [name, desc] : CMDS) {
        std::cout << "  " << std::left;
        std::cout.width(12);
        std::cout << name << desc << "\n";
    }
    std::cout <<
        "\nGlobal flags:\n"
        "  --verbose, -v   Verbose output\n"
        "  --version       Show version\n"
        "  --help, -h      Show this help\n";
}

void Dispatcher::printVersion() const {
    std::cout << "icmg " << icmg::core::ICMG_VERSION << "\n";
}

} // namespace icmg::cli
