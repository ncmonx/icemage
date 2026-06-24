// src/cli/commands/smart_read_cmd.cpp
// v2.8.3: `icmg smart-read <file>` -- auto-select reading strategy by file size.
//
// Strategy (stops at first match):
//   >50KB  -> symbols-only (large file guard; saves ~90% tokens)
//   10-50KB -> first 100 lines + truncation notice
//   <10KB  -> full content
//
// Always prefixes: [smart-read] strategy=<name> size=<N>KB

#include "../base_command.hpp"
#include "../../core/registry.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

class SmartReadCommand : public BaseCommand {
public:
    std::string name()        const override { return "smart-read"; }
    std::string description() const override {
        return "Auto-select reading strategy by file size (symbols/truncated/full)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg smart-read <file> [options]\n\n"
            "Automatically selects the most token-efficient reading strategy:\n"
            "  >50KB   symbols-only  (saves ~90% tokens on large files)\n"
            "  10-50KB first 100 lines + truncation notice\n"
            "  <10KB   full content\n\n"
            "Options:\n"
            "  --force    Always emit full content regardless of size\n"
            "  --limit N  Override max lines for truncated mode (default 100)\n"
            "  --quiet    Suppress [smart-read] prefix line\n\n"
            "Exit: 0=ok, 1=file not found, 2=usage error\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }

        std::string file_path = args[0];
        bool force   = hasFlag(args, "--force");
        bool quiet   = hasFlag(args, "--quiet");
        int  limit   = 100;
        try {
            std::string lv = flagValue(args, "--limit", "100");
            if (!lv.empty()) limit = std::stoi(lv);
        } catch (...) {}

        fs::path fp(file_path);
        if (!fs::exists(fp)) {
            std::cerr << "smart-read: file not found: " << file_path << "\n";
            return 1;
        }

        std::error_code ec;
        uintmax_t bytes = fs::file_size(fp, ec);
        if (ec) bytes = 0;
        double kb = bytes / 1024.0;

        // Determine strategy
        std::string strategy;
        if (force)       strategy = "full";
        else if (kb > 50) strategy = "symbols-only";
        else if (kb > 10) strategy = "truncated";
        else              strategy = "full";

        if (!quiet) {
            std::cout << "[smart-read] strategy=" << strategy
                      << " size=" << (int)kb << "KB"
                      << " file=" << file_path << "\n";
        }

        if (strategy == "symbols-only") {
            // Large file: emit notice + delegate to icmg context --symbols-only
            std::cout << "[smart-read] File is large (" << (int)kb
                      << "KB). Use `icmg context " << file_path
                      << " --symbols-only` for symbol list, or `icmg context "
                      << file_path << " --skeleton` for signatures.\n";
            // Emit first 20 lines as a minimal anchor
            std::ifstream f(fp);
            std::string line;
            int n = 0;
            while (n < 20 && std::getline(f, line)) {
                std::cout << line << "\n";
                ++n;
            }
            if (n == 20)
                std::cout << "... [" << (int)kb << "KB truncated — "
                          << "use icmg context " << file_path << " for full context]\n";
            return 0;
        }

        if (strategy == "truncated") {
            std::ifstream f(fp);
            std::string line;
            int n = 0;
            while (n < limit && std::getline(f, line)) {
                std::cout << line << "\n";
                ++n;
            }
            if (n == limit) {
                std::cout << "\n[smart-read] truncated at " << limit
                          << " lines (" << (int)kb << "KB file). "
                          << "Use `icmg context " << file_path
                          << "` for full context or `--force` to read all.\n";
            }
            return 0;
        }

        // full
        std::ifstream f(fp, std::ios::binary);
        if (!f) {
            std::cerr << "smart-read: cannot read: " << file_path << "\n";
            return 1;
        }
        std::ostringstream ss; ss << f.rdbuf();
        std::cout << ss.str();
        return 0;
    }
};

ICMG_REGISTER_COMMAND("smart-read", SmartReadCommand);

} // namespace icmg::cli
