// src/cli/commands/fuzzy_edit_cmd.cpp
// v2.8.1: `icmg fuzzy-edit` — whitespace-tolerant file edit.
//
// Solves the "old_string not found" retry loop: when the agent's old_string
// has wrong indentation (2-space vs 4-space), CRLF vs LF, or minor whitespace
// drift, native Edit fails silently and wastes a full round-trip.
//
// This command applies a 3-level matching cascade:
//   L1 exact -> L2 whitespace-normalised -> L3 anchor-line
// and reports which level matched (or the closest hint if none).
//
// Usage:
//   icmg fuzzy-edit <file> --old "<old_string>" --new "<new_string>"
//   icmg fuzzy-edit <file> --old-file <old.txt> --new-file <new.txt>
//   icmg fuzzy-edit <file> --old "<old>" --new "<new>" --dry-run
//
// Exit codes: 0=applied, 1=not found (hint printed), 2=usage error.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../fuzzy_edit.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {
std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
bool writeFile(const fs::path& p, const std::string& content) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f << content; return true;
}
} // namespace

class FuzzyEditCommand : public BaseCommand {
public:
    std::string name()        const override { return "fuzzy-edit"; }
    std::string description() const override {
        return "Whitespace-tolerant file edit (3-level: exact -> ws-norm -> anchor)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg fuzzy-edit <file> --old <text> --new <text> [options]\n\n"
            "Applies old->new replacement with tolerance for:\n"
            "  - Indentation differences (2-space vs 4-space)\n"
            "  - CRLF vs LF line endings\n"
            "  - Leading/trailing whitespace drift\n\n"
            "Matching levels (cascade, stops at first success):\n"
            "  L1  Exact substring match (same as native Edit)\n"
            "  L2  Whitespace-normalised: strip indent per line, adapt to file\n"
            "  L3  Anchor-line: first non-empty line of old_string locates region\n\n"
            "Options:\n"
            "  --old <text>       old_string (use $'...' in bash for multiline)\n"
            "  --new <text>       new_string\n"
            "  --old-file <path>  read old_string from file instead\n"
            "  --new-file <path>  read new_string from file instead\n"
            "  --dry-run          show diff + match level, do not write file\n"
            "  --quiet            suppress info output (only errors)\n\n"
            "Exit codes: 0=applied, 1=not found, 2=usage error\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }

        std::string file_path  = args[0];
        std::string old_str    = flagValue(args, "--old",      "");
        std::string new_str    = flagValue(args, "--new",      "");
        std::string old_file   = flagValue(args, "--old-file", "");
        std::string new_file   = flagValue(args, "--new-file", "");
        bool dry_run           = hasFlag(args, "--dry-run");
        bool quiet             = hasFlag(args, "--quiet");

        if (file_path.empty()) {
            std::cerr << "fuzzy-edit: missing <file>\n"; return 2;
        }
        if (!old_file.empty()) old_str = readFile(old_file);
        if (!new_file.empty()) new_str = readFile(new_file);
        if (old_str.empty() || new_str.empty()) {
            std::cerr << "fuzzy-edit: --old and --new are required\n"; return 2;
        }

        fs::path fp(file_path);
        if (!fs::exists(fp)) {
            std::cerr << "fuzzy-edit: file not found: " << file_path << "\n";
            return 2;
        }

        std::string content = readFile(fp);
        if (content.empty()) {
            std::cerr << "fuzzy-edit: could not read " << file_path << "\n";
            return 2;
        }

        auto result = fuzzyEdit(content, old_str, new_str, dry_run);

        if (!result.ok) {
            std::cerr << "[fuzzy-edit] FAILED: " << result.hint << "\n";
            return 1;
        }

        static const char* level_names[] = {"", "exact", "ws-normalised", "anchor-line"};
        int lv = result.level >= 1 && result.level <= 3 ? result.level : 1;

        if (!quiet) {
            std::cout << "[fuzzy-edit] matched L" << lv
                      << " (" << level_names[lv] << ")";
            if (dry_run) std::cout << " [dry-run]";
            std::cout << "\n";
            if (lv > 1) {
                std::cout << "[fuzzy-edit] Note: L" << lv
                          << " match used — consider updating old_string to use "
                          << "exact file content for future edits.\n";
            }
        }

        if (!result.diff.empty() && (dry_run || lv > 1) && !quiet) {
            std::cout << result.diff;
        }

        if (!dry_run) {
            if (!writeFile(fp, result.content)) {
                std::cerr << "fuzzy-edit: write failed: " << file_path << "\n";
                return 2;
            }
            if (!quiet)
                std::cout << "[fuzzy-edit] written: " << file_path << "\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("fuzzy-edit", FuzzyEditCommand);

} // namespace icmg::cli
