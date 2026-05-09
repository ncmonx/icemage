#include "dispatcher.hpp"
#include "base_command.hpp"
#include "../core/registry.hpp"
#include "../core/config.hpp"
#include "../core/global_db.hpp"
#include "../core/project_context.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>
#ifdef _WIN32
  #include <windows.h>
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
};

Dispatcher::Dispatcher() {}

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
    std::vector<std::string> rest(cleaned.begin() + 1, cleaned.end());

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
    std::cout <<
        "icmg 0.29.3 — unified memory, graph, and Tkil tool\n\n"
        "Usage: icmg <command> [options]\n\n"
        "Commands:\n";
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
    std::cout << "icmg 0.29.3\n";
}

} // namespace icmg::cli
