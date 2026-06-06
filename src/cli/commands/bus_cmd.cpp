// `icmg bus` — me-everywhere #1: shared action stream across parallel sessions.
//
// `bus emit <kind> [--target X] [--detail "..."]` appends this session's action
// to a shared append-only log (~/.icmg/bus.tsv). `bus tail [--since <ts>]` prints
// events newer than a cursor (others' actions by default) and reports the newest
// ts so a poller can pass it back. Append-only = race-free across writers; the
// live fs-watch/daemon push is wired on top later.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/event_bus.hpp"
#include "../../core/path_utils.hpp"   // icmgGlobalDir
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <ctime>
#include <cstdlib>
#ifdef _WIN32
#  include <process.h>
#  define ICMG_GETPID _getpid
#else
#  include <unistd.h>
#  define ICMG_GETPID getpid
#endif

namespace icmg::cli {
namespace fs = std::filesystem;

class BusCommand : public BaseCommand {
    static std::string file() { return core::wireDir() + "/bus.tsv"; }
    static std::string mySession() {
        const char* s = std::getenv("ICMG_SESSION_ID");
        if (s && *s) return s;
        return "pid-" + std::to_string((long)ICMG_GETPID());
    }
    static std::string opt(const std::vector<std::string>& a, const std::string& flag) {
        for (size_t i = 1; i + 1 < a.size(); ++i) if (a[i] == flag) return a[i + 1];
        return "";
    }
    static std::vector<core::BusEvent> readAll() {
        std::vector<core::BusEvent> v;
        std::ifstream f(file());
        std::string line;
        while (std::getline(f, line)) { core::BusEvent e; if (core::eventFromLine(line, e)) v.push_back(e); }
        return v;
    }

public:
    std::string name()        const override { return "bus"; }
    std::string description() const override { return "Shared action stream across parallel sessions (emit/tail)"; }
    void usage() const override {
        std::cout << "Usage: icmg bus emit <kind> [--target <x>] [--detail \"<text>\"]\n"
                  << "       icmg bus tail [--since <ts>] [--all]\n"
                  << "  emit   record an action (kind = edit|claim|release|done|note|...)\n"
                  << "  tail   print events newer than --since (others' by default; --all incl. own)\n";
    }

    int run(const std::vector<std::string>& args) override {
        const std::string sub = args.empty() ? "" : args[0];
        const int64_t now = (int64_t)std::time(nullptr);

        if (sub == "emit") {
            std::string kind = (args.size() > 1 && !args[1].empty() && args[1][0] != '-') ? args[1] : "note";
            core::BusEvent e;
            e.ts = now; e.actor = mySession(); e.kind = kind;
            e.target = opt(args, "--target");
            e.detail = opt(args, "--detail");
            std::error_code ec; fs::create_directories(core::wireDir(), ec);
            { std::ofstream f(file(), std::ios::app); f << core::eventToLine(e) << "\n"; }
            // Opportunistic compaction: keep only the last ~500 events.
            auto all = readAll();
            if (all.size() > 1000) {
                std::ofstream w(file(), std::ios::trunc);
                for (size_t i = all.size() - 500; i < all.size(); ++i) w << core::eventToLine(all[i]) << "\n";
            }
            return 0;
        }

        if (sub == "tail") {
            int64_t since = 0;
            std::string s = opt(args, "--since");
            if (!s.empty()) { try { since = std::stoll(s); } catch (...) {} }
            else            { since = now - 120; }   // default: last 2 minutes
            bool all = false;
            for (const auto& a : args) if (a == "--all") all = true;
            auto evs = core::eventsSince(readAll(), since);
            std::string me = mySession();
            int64_t newest = since;
            int shown = 0;
            for (const auto& e : evs) {
                if (e.ts > newest) newest = e.ts;
                if (!all && e.actor == me) continue;     // skip own actions by default
                std::cout << "  [" << (now - e.ts) << "s] " << e.actor << " " << e.kind;
                if (!e.target.empty()) std::cout << " " << e.target;
                if (!e.detail.empty()) std::cout << " — " << e.detail;
                std::cout << "\n";
                ++shown;
            }
            if (shown == 0) std::cout << "(no new events)\n";
            std::cout << "cursor: " << newest << "\n";   // pass back as --since to poll
            return 0;
        }

        usage();
        return sub.empty() ? 0 : 1;
    }
};

ICMG_REGISTER_COMMAND("bus", BusCommand);

}  // namespace icmg::cli
