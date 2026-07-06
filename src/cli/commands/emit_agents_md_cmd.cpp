// `icmg emit-agents-md` — write/sync an icmg-managed routing block into
// AGENTS.md so every non-Claude coding agent that reads the AGENTS.md standard
// (Cursor, Windsurf, GitHub Copilot, OpenAI Codex, Jules, RooCode, Gemini CLI,
// Aider, Zed, VS Code, Devin, JetBrains Junie) inherits icmg-first behavior.
//
// Feature #2 (DO FIRST) from docs/plans/2026-07-04-feature-research-2026-landscape.md.
// Merge semantics are pure + unit-tested in src/cli/emit_agents_md.hpp.
//
// Idempotent: the icmg block lives between HTML-comment markers; re-running
// replaces only that span and never clobbers hand-written user content.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../emit_agents_md.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace icmg::cli {

class EmitAgentsMdCommand : public BaseCommand {
public:
    std::string name() const override { return "emit-agents-md"; }
    std::string description() const override {
        return "Sync an icmg-first routing block into AGENTS.md (cross-tool agent config)";
    }

    void usage() const override {
        std::cout << "Usage: icmg emit-agents-md [--path <file>] [--check] [--stdout]\n"
                  << "  Write/sync the icmg routing block into AGENTS.md (default: ./AGENTS.md).\n"
                  << "  --path <file>  target file (default AGENTS.md)\n"
                  << "  --check        exit 1 if the file is out of sync (no write); CI gate\n"
                  << "  --stdout       print merged result to stdout, do not write\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        namespace fs = std::filesystem;

        std::string path = flagValue(args, "--path", "AGENTS.md");
        bool check   = hasFlag(args, "--check");
        bool toStdout = hasFlag(args, "--stdout");

        std::string existing;
        if (fs::exists(path)) {
            std::ifstream in(path, std::ios::binary);
            std::ostringstream ss; ss << in.rdbuf();
            existing = ss.str();
        }

        const std::string merged = syncAgentsMd(existing, icmgRoutingBlock());

        if (toStdout) { std::cout << merged; return 0; }

        if (check) {
            if (merged != existing) {
                std::cerr << "emit-agents-md: " << path
                          << " is OUT OF SYNC (run `icmg emit-agents-md` to fix)\n";
                return 1;
            }
            std::cout << "emit-agents-md: " << path << " is in sync\n";
            return 0;
        }

        if (merged == existing) {
            std::cout << "emit-agents-md: " << path << " already in sync (no change)\n";
            return 0;
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::cerr << "emit-agents-md: cannot write " << path << "\n";
            return 1;
        }
        out << merged;
        std::cout << "emit-agents-md: synced icmg routing block -> " << path << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("emit-agents-md", EmitAgentsMdCommand);

} // namespace icmg::cli
