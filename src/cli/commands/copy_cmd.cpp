// `icmg copy` — copy file content without Claude generating output tokens.
//
// Eliminates output-token cost when Claude needs to create or extend a file
// using content that already exists in the codebase.
//
// Usage:
//   icmg copy --from <src> [--lines A-B] [--to <dest>] [--append] [--insert-at N]
//   icmg copy --from <src> --to <dest>          # full file clone
//   icmg copy --from <src> --lines 10-50        # print range to stdout
//   icmg copy --from <src> --lines 10-50 --to <dest> --append
//   icmg copy --from <src> --lines 10-50 --to <dest> --insert-at 25

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

class CopyCommand : public BaseCommand {
public:
    std::string name()        const override { return "copy"; }
    std::string description() const override { return "Copy file content (line range) without output-token cost"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg copy --from <src> [options]\n\n"
            "Options:\n"
            "  --from <file>        Source file (required)\n"
            "  --lines A-B          Line range to copy (1-indexed, inclusive)\n"
            "  --to <file>          Destination file (default: stdout)\n"
            "  --append             Append to destination instead of overwrite\n"
            "  --insert-at <N>      Insert before line N in destination\n"
            "  --dry-run            Show what would be written without writing\n\n"
            "Examples:\n"
            "  icmg copy --from src/auth.cpp --lines 1-40 --to tests/test_auth.cpp\n"
            "  icmg copy --from template.cpp --to new_cmd.cpp\n"
            "  icmg copy --from base.cpp --lines 5-30 --to target.cpp --insert-at 10\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage(); return 0;
        }

        std::string src     = flagValue(args, "--from");
        std::string dest    = flagValue(args, "--to");
        std::string lines_s = flagValue(args, "--lines");
        std::string ins_s   = flagValue(args, "--insert-at");
        bool append         = hasFlag(args, "--append");
        bool dry_run        = hasFlag(args, "--dry-run");

        if (src.empty()) {
            std::cerr << "icmg copy: --from <file> required\n";
            return 1;
        }

        // Parse --lines A-B
        int line_from = 0, line_to = INT_MAX;
        if (!lines_s.empty()) {
            auto dash = lines_s.find('-');
            if (dash == std::string::npos) {
                std::cerr << "icmg copy: --lines format must be A-B (e.g. 10-50)\n";
                return 1;
            }
            try {
                line_from = std::stoi(lines_s.substr(0, dash));
                line_to   = std::stoi(lines_s.substr(dash + 1));
            } catch (...) {
                std::cerr << "icmg copy: --lines values must be integers\n";
                return 1;
            }
            if (line_from < 1 || line_to < line_from) {
                std::cerr << "icmg copy: --lines A-B requires A >= 1 and B >= A\n";
                return 1;
            }
        }

        // Read source
        std::ifstream fin(src);
        if (!fin) {
            std::cerr << "icmg copy: cannot read source: " << src << "\n";
            return 1;
        }
        std::vector<std::string> src_lines;
        std::string line;
        while (std::getline(fin, line)) src_lines.push_back(line);

        // Extract slice
        int total = (int)src_lines.size();
        int lf = (line_from > 0) ? line_from : 1;
        int lt = (line_to < INT_MAX) ? std::min(line_to, total) : total;

        std::ostringstream chunk;
        for (int i = lf; i <= lt && i <= total; ++i)
            chunk << src_lines[i - 1] << "\n";
        std::string content = chunk.str();

        int copied_lines = lt - lf + 1;
        if (copied_lines < 0) copied_lines = 0;

        // Stdout mode
        if (dest.empty()) {
            if (dry_run) {
                std::cout << "[dry-run] would print " << copied_lines
                          << " lines from " << src;
                if (!lines_s.empty()) std::cout << " (lines " << lf << "-" << lt << ")";
                std::cout << "\n";
                return 0;
            }
            std::cout << content;
            return 0;
        }

        // Insert-at mode: splice content into existing destination
        if (!ins_s.empty()) {
            int ins_line = 0;
            try { ins_line = std::stoi(ins_s); } catch (...) {}
            if (ins_line < 1) {
                std::cerr << "icmg copy: --insert-at must be >= 1\n";
                return 1;
            }

            std::vector<std::string> dest_lines;
            std::ifstream din(dest);
            if (din) {
                std::string dl;
                while (std::getline(din, dl)) dest_lines.push_back(dl);
            }

            std::ostringstream out;
            int before = std::min(ins_line - 1, (int)dest_lines.size());
            for (int i = 0; i < before; ++i)        out << dest_lines[i] << "\n";
            out << content;
            for (int i = before; i < (int)dest_lines.size(); ++i) out << dest_lines[i] << "\n";

            if (dry_run) {
                std::cout << "[dry-run] would insert " << copied_lines
                          << " lines before line " << ins_line
                          << " in " << dest << "\n";
                return 0;
            }
            std::ofstream fout(dest);
            if (!fout) { std::cerr << "icmg copy: cannot write: " << dest << "\n"; return 1; }
            fout << out.str();
            std::cout << "Inserted " << copied_lines << " line(s) before line "
                      << ins_line << " in " << dest << "\n";
            return 0;
        }

        // Append or overwrite mode
        if (dry_run) {
            std::cout << "[dry-run] would " << (append ? "append" : "write") << " "
                      << copied_lines << " lines";
            if (!lines_s.empty()) std::cout << " (lines " << lf << "-" << lt << ")";
            std::cout << " from " << src << " to " << dest << "\n";
            return 0;
        }

        auto mode = append ? std::ios::app : (std::ios::out | std::ios::trunc);
        std::ofstream fout(dest, mode);
        if (!fout) { std::cerr << "icmg copy: cannot write: " << dest << "\n"; return 1; }
        fout << content;

        std::cout << (append ? "Appended " : "Wrote ") << copied_lines << " line(s)";
        if (!lines_s.empty()) std::cout << " (lines " << lf << "-" << lt << ")";
        std::cout << " from " << src << " to " << dest << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("copy", CopyCommand);

} // namespace icmg::cli
