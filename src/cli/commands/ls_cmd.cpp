// `icmg ls [path] [options]` — directory listing with token-friendly output.
//
// Why a native command: spawning ls via icmg run loses Windows path quoting +
// produces unfiltered noise on big directories. `icmg ls` uses std::filesystem
// directly, groups by kind (dirs first), truncates name lists at 50 entries,
// and surfaces hidden via --all.
//
// Examples:
//   icmg ls                        # current dir
//   icmg ls .claude                # path arg (quoting handled)
//   icmg ls "C:/Path With Spaces"  # spaces ok
//   icmg ls --all                  # include dotfiles
//   icmg ls --json                 # machine output
//   icmg ls --tree                 # one level deep, indented

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace icmg::cli {

class LsCommand : public BaseCommand {
public:
    std::string name()        const override { return "ls"; }
    std::string description() const override { return "Token-friendly directory listing"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg ls [path] [options]\n\n"
            "Options:\n"
            "  --all, -a       Include hidden (dotfiles)\n"
            "  --tree          One-level tree view\n"
            "  --json          JSON output\n"
            "  --limit N       Max entries (default 100)\n"
            "  --ext E         Filter by extension (e.g. --ext cs)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }
        bool show_hidden = hasFlag(args, "--all") || hasFlag(args, "-a");
        bool tree        = hasFlag(args, "--tree");
        bool json        = hasFlag(args, "--json");
        int  limit       = 100;
        try { limit = std::stoi(flagValue(args, "--limit", "100")); } catch (...) {}
        std::string ext_filter = flagValue(args, "--ext", "");
        if (!ext_filter.empty() && ext_filter[0] != '.') ext_filter = "." + ext_filter;

        // Track positional arg, skipping values consumed by valued flags.
        static const std::vector<std::string> valued = {"--limit", "--ext"};
        std::string target = ".";
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a.empty()) continue;
            if (a[0] == '-') {
                for (auto& v : valued) {
                    if (a == v) { ++i; break; }
                }
                continue;
            }
            target = a; break;
        }

        std::error_code ec;
        fs::path root = fs::weakly_canonical(target, ec);
        if (ec || !fs::exists(root)) {
            std::cerr << "icmg ls: not found: " << target << "\n";
            return 1;
        }
        if (!fs::is_directory(root)) {
            std::cerr << "icmg ls: not a directory: " << root.string() << "\n";
            return 1;
        }

        struct Entry {
            std::string name;
            bool is_dir = false;
            uintmax_t size = 0;
            std::string ext;
        };
        std::vector<Entry> dirs, files;

        for (auto it = fs::directory_iterator(root, ec); it != fs::directory_iterator(); ++it) {
            if (ec) break;
            const auto& p = it->path();
            std::string nm = p.filename().string();
            if (!show_hidden && !nm.empty() && nm[0] == '.') continue;
            Entry e;
            e.name   = nm;
            e.is_dir = it->is_directory(ec);
            if (!e.is_dir) {
                e.size = it->file_size(ec); if (ec) e.size = 0;
                e.ext  = p.extension().string();
            }
            if (!ext_filter.empty() && !e.is_dir && e.ext != ext_filter) continue;
            (e.is_dir ? dirs : files).push_back(std::move(e));
        }

        auto cmp = [](const Entry& a, const Entry& b) { return a.name < b.name; };
        std::sort(dirs.begin(),  dirs.end(),  cmp);
        std::sort(files.begin(), files.end(), cmp);

        int total = (int)dirs.size() + (int)files.size();
        int shown = 0;

        if (json) {
            std::cout << "{\"path\":\"" << root.string() << "\",\"dirs\":[";
            for (size_t i = 0; i < dirs.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "\"" << dirs[i].name << "\"";
            }
            std::cout << "],\"files\":[";
            for (size_t i = 0; i < files.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "{\"name\":\"" << files[i].name
                          << "\",\"size\":" << files[i].size << "}";
            }
            std::cout << "],\"total\":" << total << "}\n";
            return 0;
        }

        if (tree) {
            std::cout << root.string() << "\n";
            for (auto& d : dirs) {
                if (++shown > limit) break;
                std::cout << "  " << d.name << "/\n";
            }
            for (auto& f : files) {
                if (++shown > limit) break;
                std::cout << "  " << f.name << "\n";
            }
        } else {
            for (auto& d : dirs) {
                if (++shown > limit) break;
                std::cout << d.name << "/\n";
            }
            for (auto& f : files) {
                if (++shown > limit) break;
                std::cout << std::left << std::setw(40) << f.name
                          << "  " << humanSize(f.size) << "\n";
            }
        }

        if (total > shown) {
            std::cout << "... " << (total - shown) << " more (--limit " << total << " to see all)\n";
        } else {
            std::cout << "(" << dirs.size() << " dirs, " << files.size() << " files)\n";
        }
        return 0;
    }

private:
    static std::string humanSize(uintmax_t n) {
        const char* units[] = {"B", "K", "M", "G", "T"};
        int u = 0; double d = (double)n;
        while (d >= 1024.0 && u < 4) { d /= 1024.0; ++u; }
        char buf[32];
        if (u == 0) std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)n);
        else        std::snprintf(buf, sizeof(buf), "%.1f%s", d, units[u]);
        return buf;
    }
};

ICMG_REGISTER_COMMAND("ls", LsCommand);

} // namespace icmg::cli
