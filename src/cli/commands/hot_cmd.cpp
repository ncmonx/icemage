// `icmg hot` — me-everywhere #5: shared hot working-memory (whiteboard).
//
// `hot set <key> <value...>` writes a shared key; `hot get <key>` reads it;
// `hot list` shows the whole board; `hot clear` empties it. Append-only log
// (~/.icmg/hot.tsv) collapsed to last-write-wins per key. A scratch tier for
// collaborating sessions; the truly RAM-volatile version arrives with the daemon.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/hot.hpp"
#include "../../core/path_utils.hpp"   // icmgGlobalDir
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <ctime>

namespace icmg::cli {
namespace fs = std::filesystem;

class HotCommand : public BaseCommand {
    static std::string file() { return core::wireDir() + "/hot.tsv"; }
    static std::vector<core::HotEntry> readAll() {
        std::vector<core::HotEntry> v;
        std::ifstream f(file());
        std::string line;
        while (std::getline(f, line)) { core::HotEntry e; if (core::hotFromLine(line, e)) v.push_back(e); }
        return v;
    }

public:
    std::string name()        const override { return "hot"; }
    std::string description() const override { return "Shared hot working-memory across sessions (set/get/list/clear)"; }
    void usage() const override {
        std::cout << "Usage: icmg hot set <key> <value...> | get <key> | list | clear\n"
                  << "  A volatile shared whiteboard for collaborating sessions on this machine.\n";
    }

    int run(const std::vector<std::string>& args) override {
        const std::string sub = args.empty() ? "" : args[0];
        const int64_t now = (int64_t)std::time(nullptr);

        if (sub == "set") {
            if (args.size() < 3) { std::cerr << "icmg hot set: need <key> and <value>\n"; return 1; }
            std::string key = args[1], val;
            for (size_t i = 2; i < args.size(); ++i) { if (i > 2) val += ' '; val += args[i]; }
            core::HotEntry e; e.key = key; e.ts = now; e.value = val;
            std::error_code ec; fs::create_directories(core::wireDir(), ec);
            { std::ofstream f(file(), std::ios::app); f << core::hotToLine(e) << "\n"; }
            auto all = readAll();
            if (all.size() > 500) {   // compact to current board
                auto cur = core::latestPerKey(all);
                std::ofstream w(file(), std::ios::trunc);
                for (const auto& x : cur) w << core::hotToLine(x) << "\n";
            }
            return 0;
        }

        if (sub == "get") {
            if (args.size() < 2) { std::cerr << "icmg hot get: key required\n"; return 1; }
            for (const auto& e : core::latestPerKey(readAll()))
                if (e.key == args[1]) { std::cout << e.value << "\n"; return 0; }
            return 1;   // not found
        }

        if (sub == "list") {
            auto cur = core::latestPerKey(readAll());
            if (cur.empty()) { std::cout << "(whiteboard empty)\n"; return 0; }
            for (const auto& e : cur)
                std::cout << "  " << e.key << " = " << e.value
                          << "  (" << (now - e.ts) << "s ago)\n";
            return 0;
        }

        if (sub == "clear") {
            std::error_code ec; fs::remove(file(), ec);
            std::cout << "whiteboard cleared\n";
            return 0;
        }

        usage();
        return sub.empty() ? 0 : 1;
    }
};

ICMG_REGISTER_COMMAND("hot", HotCommand);

}  // namespace icmg::cli
