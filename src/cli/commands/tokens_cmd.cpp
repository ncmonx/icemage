// `icmg tokens` — approximate token counter for files, directories, or stdin.
//
// Uses a local heuristic (no network, no external deps) that targets ±20%
// of tiktoken cl100k_base for typical English/code content.
//
// Usage:
//   icmg tokens <file>              # prints "N tokens (B bytes)"
//   icmg tokens --json <file>       # prints JSON object
//   icmg tokens --per-file <dir>    # per-file lines + total
//   icmg tokens                     # reads stdin
//   icmg tokens --help

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/token_counter.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

class TokensCommand : public BaseCommand {
public:
    std::string name()        const override { return "tokens"; }
    std::string description() const override {
        return "Estimate token count for a file, directory, or stdin (heuristic, no network)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg tokens [options] [path]\n\n"
            "Options:\n"
            "  --json          Output JSON instead of human-readable text\n"
            "  --per-file      Walk a directory and show per-file counts + total\n"
            "  --help, -h      Show this help\n\n"
            "Examples:\n"
            "  icmg tokens README.md\n"
            "  icmg tokens --json src/main.cpp\n"
            "  icmg tokens --per-file src/\n"
            "  cat large.txt | icmg tokens\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage(); return 0;
        }

        bool json_mode    = hasFlag(args, "--json");
        bool per_file     = hasFlag(args, "--per-file");

        // Collect positional args (not flags)
        std::vector<std::string> positional;
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a == "--json" || a == "--per-file") continue;
            if (!a.empty() && a[0] == '-') continue;
            positional.push_back(a);
        }

        // No path: read stdin
        if (positional.empty()) {
            std::ostringstream ss;
            ss << std::cin.rdbuf();
            std::string text = ss.str();
            size_t tokens = core::estimateTokens(text);
            size_t bytes  = text.size();
            if (json_mode) {
                std::cout << "{\"path\":\"<stdin>\",\"tokens\":" << tokens
                          << ",\"bytes\":" << bytes << "}\n";
            } else {
                std::cout << tokens << " tokens (" << bytes << " bytes)\n";
            }
            return 0;
        }

        std::string path_str = positional[0];
        fs::path p(path_str);

        if (!fs::exists(p)) {
            std::cerr << "icmg tokens: path not found: " << path_str << "\n";
            return 1;
        }

        // Directory walk mode
        if (fs::is_directory(p) || per_file) {
            if (!fs::is_directory(p)) {
                std::cerr << "icmg tokens: --per-file requires a directory\n";
                return 1;
            }
            size_t total_tokens = 0;
            size_t total_bytes  = 0;
            for (auto& entry : fs::recursive_directory_iterator(p,
                    fs::directory_options::skip_permission_denied)) {
                if (!entry.is_regular_file()) continue;
                std::ifstream fin(entry.path(), std::ios::binary);
                if (!fin) continue;
                std::ostringstream ss;
                ss << fin.rdbuf();
                std::string text = ss.str();
                size_t t = core::estimateTokens(text);
                size_t b = text.size();
                total_tokens += t;
                total_bytes  += b;
                if (json_mode) {
                    // Escape backslashes in path for JSON
                    std::string ps = entry.path().string();
                    for (auto& c : ps) if (c == '\\') c = '/';
                    std::cout << "{\"path\":\"" << ps << "\",\"tokens\":"
                              << t << ",\"bytes\":" << b << "}\n";
                } else {
                    std::cout << t << " tokens\t" << entry.path().string() << "\n";
                }
            }
            if (json_mode) {
                std::cout << "{\"path\":\"" << path_str << "\",\"total_tokens\":"
                          << total_tokens << ",\"total_bytes\":" << total_bytes << "}\n";
            } else {
                std::cout << "---\n" << total_tokens << " tokens total ("
                          << total_bytes << " bytes)\n";
            }
            return 0;
        }

        // Single file
        std::ifstream fin(p, std::ios::binary);
        if (!fin) {
            std::cerr << "icmg tokens: cannot read: " << path_str << "\n";
            return 1;
        }
        std::ostringstream ss;
        ss << fin.rdbuf();
        std::string text = ss.str();
        size_t tokens = core::estimateTokens(text);
        size_t bytes  = text.size();

        if (json_mode) {
            std::string ps = p.string();
            for (auto& c : ps) if (c == '\\') c = '/';
            std::cout << "{\"path\":\"" << ps << "\",\"tokens\":"
                      << tokens << ",\"bytes\":" << bytes << "}\n";
        } else {
            std::cout << tokens << " tokens (" << bytes << " bytes)\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("tokens", TokensCommand);

} // namespace icmg::cli
