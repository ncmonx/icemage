// `icmg presence` — live mutual-awareness across parallel sessions on one machine.
//
// me-everywhere step 1 (wiring). Each session `beat`s its current focus to a
// shared append-only log; any session `list`s who is live (heartbeat within TTL)
// and what they hold. Append-only = no read-modify-write race between writers;
// readers collapse to the latest beat per session. This is the visible-presence
// layer; the deeper live event bus (carried by the RAM-brain daemon) builds on it.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/presence.hpp"
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

class PresenceCommand : public BaseCommand {
    static std::string file()   { return core::wireDir() + "/presence.tsv"; }
    static std::string mySession() {
        const char* s = std::getenv("ICMG_SESSION_ID");
        if (s && *s) return s;
        return "pid-" + std::to_string((long)ICMG_GETPID());
    }
    static std::vector<core::PresenceEntry> readAll() {
        std::vector<core::PresenceEntry> v;
        std::ifstream f(file());
        std::string line;
        while (std::getline(f, line)) {
            core::PresenceEntry e;
            if (core::presenceFromLine(line, e)) v.push_back(e);
        }
        return v;
    }

public:
    std::string name()        const override { return "presence"; }
    std::string description() const override { return "Live mutual-awareness across parallel sessions (beat/list)"; }
    void usage() const override {
        std::cout << "Usage: icmg presence beat [--focus \"<task>\"] | list [--ttl <sec>]\n"
                  << "  beat   record this session's heartbeat + current focus\n"
                  << "  list   show sessions whose heartbeat is within TTL (default 90s)\n";
    }

    int run(const std::vector<std::string>& args) override {
        const std::string sub = args.empty() ? "" : args[0];
        const int64_t now = (int64_t)std::time(nullptr);

        if (sub == "beat") {
            std::string focus;
            for (size_t i = 1; i + 1 < args.size(); ++i)
                if (args[i] == "--focus") focus = args[i + 1];
            core::PresenceEntry e;
            e.session_id = mySession();
            e.pid = (int64_t)ICMG_GETPID();
            e.focus = focus;
            e.heartbeat_at = now;

            std::error_code ec; fs::create_directories(core::wireDir(), ec);
            { std::ofstream f(file(), std::ios::app); f << core::presenceToLine(e) << "\n"; }

            // Opportunistic compaction once the append log grows: keep only the
            // latest live entry per session.
            auto all = readAll();
            if (all.size() > 200) {
                auto live = core::livePresence(core::latestPerSession(all), now, 300);
                std::ofstream w(file(), std::ios::trunc);
                for (const auto& x : live) w << core::presenceToLine(x) << "\n";
            }
            return 0;
        }

        if (sub == "list") {
            int64_t ttl = 90;
            for (size_t i = 1; i + 1 < args.size(); ++i)
                if (args[i] == "--ttl") { try { ttl = std::stoll(args[i + 1]); } catch (...) {} }
            auto live = core::livePresence(core::latestPerSession(readAll()), now, ttl);
            if (live.empty()) { std::cout << "(no live sessions)\n"; return 0; }
            std::cout << "Live sessions (heartbeat <= " << ttl << "s):\n";
            for (const auto& e : live)
                std::cout << "  " << e.session_id << "  pid=" << e.pid
                          << "  (" << (now - e.heartbeat_at) << "s ago)  " << e.focus << "\n";
            return 0;
        }

        if (sub == "sync") {
            // One-shot for the heartbeat hook: beat THIS session, then print a
            // one-line summary of the OTHER live sessions (empty if alone). The
            // hook wraps the line via `icmg hookio emit` so the agent sees who
            // else is working each turn — presence, always prioritized.
            std::string focus;
            for (size_t i = 1; i + 1 < args.size(); ++i)
                if (args[i] == "--focus") focus = args[i + 1];
            core::PresenceEntry e;
            e.session_id = mySession(); e.pid = (int64_t)ICMG_GETPID();
            e.focus = focus; e.heartbeat_at = now;
            std::error_code ec; fs::create_directories(core::wireDir(), ec);
            { std::ofstream f(file(), std::ios::app); f << core::presenceToLine(e) << "\n"; }

            auto live = core::livePresence(core::latestPerSession(readAll()), now, 90);
            std::string me = mySession();
            std::string others;
            int n = 0;
            for (const auto& x : live) {
                if (x.session_id == me) continue;
                if (n++) others += "; ";
                others += x.session_id;
                if (!x.focus.empty()) others += " (" + x.focus + ")";
            }
            if (n > 0)
                std::cout << "[me-everywhere] " << n << " other live session(s): " << others << "\n";
            return 0;
        }

        usage();
        return sub.empty() ? 0 : 1;
    }
};

ICMG_REGISTER_COMMAND("presence", PresenceCommand);

}  // namespace icmg::cli
