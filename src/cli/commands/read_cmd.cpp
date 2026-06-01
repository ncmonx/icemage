// v1.28.0 #F: `icmg read <file>` — file slicer with regex-anchored window.
//
// Motivation: hook STRICT cap on Read tool is 30 lines (Edit-anchor only).
// For Edit operations against a target buried mid-file, AI needs ±100
// lines around an anchor regex. `icmg read --around 'class FooBar'` returns
// that window without forcing AI to paginate manually.
//
// Default window = 100 lines (50 before, 50 after). Override via --window N.
// First match wins; for multi-match use --all (caps at 5 windows).

#include "../base_command.hpp"
#include "../../core/registry.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

class ReadCommand : public BaseCommand {
public:
    std::string name()        const override { return "read"; }
    std::string description() const override {
        return "Read file slice — regex-anchored window or line range";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg read <file> [options]\n\n"
            "Options:\n"
            "  --around <regex>   Return window around first regex match\n"
            "  --window N         Window size in lines (default 100; 50 above + 50 below)\n"
            "  --all              Return ALL matches (capped at 5 windows)\n"
            "  --lines A-B        Return lines A through B (1-indexed)\n"
            "  --raw              No headers/markers — emit raw content\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage(); return 0;
        }
        std::string file;
        for (auto& a : args) if (!a.empty() && a[0] != '-') { file = a; break; }
        if (file.empty()) {
            std::cerr << "icmg read: requires <file>\n"; return 1;
        }
        // v1.28.0 #D: same drive-letter normalize as `icmg context`.
        if (file.size() >= 2 && file[1] == ':'
            && ((file[0] >= 'A' && file[0] <= 'Z') || (file[0] >= 'a' && file[0] <= 'z'))
            && (file.size() == 2 || (file[2] != '/' && file[2] != '\\'))) {
            file = file.substr(0, 2) + "/" + file.substr(2);
        }
        for (auto& c : file) if (c == '\\') c = '/';

        if (!fs::exists(file)) {
            std::cerr << "icmg read: not found: " << file << "\n"; return 1;
        }

        // Load all lines (1-indexed for display).
        std::vector<std::string> lines;
        {
            std::ifstream f(file, std::ios::binary);
            if (!f) { std::cerr << "icmg read: cannot open: " << file << "\n"; return 1; }
            std::string line;
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(std::move(line));
            }
        }
        int total = (int)lines.size();

        int window = 100;
        try { window = std::stoi(flagValue(args, "--window", "100")); } catch (...) {}
        if (window < 4) window = 4;
        int half = window / 2;

        bool raw  = hasFlag(args, "--raw");
        bool all  = hasFlag(args, "--all");
        std::string around = flagValue(args, "--around");
        std::string lines_arg = flagValue(args, "--lines");

        auto emitSlice = [&](int from1, int to1, const std::string& tag) {
            if (from1 < 1) from1 = 1;
            if (to1 > total) to1 = total;
            if (!raw) {
                std::cout << "=== " << file << " lines " << from1 << "-" << to1
                          << (tag.empty() ? "" : " (" + tag + ")") << " ===\n";
            }
            for (int i = from1; i <= to1; ++i) {
                if (!raw) std::cout << i << "\t";
                std::cout << lines[i - 1] << "\n";
            }
            if (!raw) std::cout << "\n";
        };

        // Priority: --lines explicit > --around regex > whole-file dump.
        if (!lines_arg.empty()) {
            int dash = (int)lines_arg.find('-');
            if (dash <= 0) { std::cerr << "icmg read: --lines A-B required\n"; return 1; }
            int from = 1, to = total;
            try { from = std::stoi(lines_arg.substr(0, dash));
                  to   = std::stoi(lines_arg.substr(dash + 1)); } catch (...) {}
            emitSlice(from, to, "");
            return 0;
        }

        if (!around.empty()) {
            std::regex re;
            try { re = std::regex(around, std::regex::ECMAScript); }
            catch (const std::exception& e) {
                std::cerr << "icmg read: bad regex: " << e.what() << "\n";
                return 1;
            }
            int hits = 0;
            for (int i = 0; i < total; ++i) {
                if (std::regex_search(lines[i], re)) {
                    int center = i + 1;
                    int from = center - half;
                    int to   = center + half;
                    emitSlice(from, to, "match @" + std::to_string(center));
                    ++hits;
                    if (!all) break;
                    if (hits >= 5) {
                        if (!raw) std::cout << "(more matches suppressed; pass --all and accept >5 if needed)\n";
                        break;
                    }
                }
            }
            if (hits == 0) {
                std::cerr << "icmg read: --around regex matched 0 lines in " << file << "\n";
                return 1;
            }
            return 0;
        }

        // No filter — emit whole file with markers (unless --raw).
        emitSlice(1, total, "full");
        return 0;
    }
};

ICMG_REGISTER_COMMAND("read", ReadCommand);

}  // namespace icmg::cli
