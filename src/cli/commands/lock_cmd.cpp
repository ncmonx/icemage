// `icmg lock` — me-everywhere #3: resource locks across parallel sessions.
//
// `lock claim <scope>` claims a file/zone/task so another live session can't
// clobber it; `lock release <scope>` frees it; `lock list` shows held scopes.
// Backed by the pure resolveClaim (agent_lease.hpp) over a shared append-only
// log (~/.icmg/locks.tsv). Ownership is by session id (the `host` field); a
// release appends a heartbeat=0 tombstone that supersedes the claim. Append-only
// = race-free across writers. Makes parallel sessions SAFE, not just visible.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/agent_lease.hpp"
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

class LockCommand : public BaseCommand {
    static constexpr int64_t TTL = 300;   // a claim is live if beaten within 5 min
    static std::string file() { return core::wireDir() + "/locks.tsv"; }
    static std::string mySession() {
        const char* s = std::getenv("ICMG_SESSION_ID");
        if (s && *s) return s;
        return "pid-" + std::to_string((long)ICMG_GETPID());
    }
    static std::vector<core::AgentLease> readAll() {
        std::vector<core::AgentLease> v;
        std::ifstream f(file());
        std::string line;
        while (std::getline(f, line)) { core::AgentLease l; if (core::leaseFromLine(line, l)) v.push_back(l); }
        return v;
    }
    static void append(const core::AgentLease& l) {
        std::error_code ec; fs::create_directories(core::wireDir(), ec);
        std::ofstream f(file(), std::ios::app); f << core::leaseToLine(l) << "\n";
    }

public:
    std::string name()        const override { return "lock"; }
    std::string description() const override { return "Resource locks across parallel sessions (claim/release/list)"; }
    void usage() const override {
        std::cout << "Usage: icmg lock claim <scope> | release <scope> | list\n"
                  << "  claim    claim a file/zone/task so another live session can't clobber it\n"
                  << "  release  free a scope you hold\n"
                  << "  list     show currently-held scopes + owner\n";
    }

    int run(const std::vector<std::string>& args) override {
        const std::string sub = args.empty() ? "" : args[0];
        const std::string scope = (args.size() > 1) ? args[1] : "";
        const int64_t now = (int64_t)std::time(nullptr);
        const std::string me = mySession();

        if (sub == "claim" || sub == "release") {
            if (scope.empty()) { std::cerr << "icmg lock " << sub << ": scope required\n"; return 1; }
            if (sub == "release") {
                append(core::AgentLease{scope, 0, me, 0});   // tombstone (heartbeat 0 = stale)
                std::cout << "released '" << scope << "'\n";
                return 0;
            }
            auto held = core::lastPerScope(readAll());
            auto r = core::resolveClaim(held, scope, /*my_pid=*/0, me, now, TTL);
            if (!r.granted) {
                std::cout << "DENIED '" << scope << "' — held by " << r.conflict_host << "\n";
                return 1;
            }
            append(core::AgentLease{scope, 0, me, now});
            std::cout << "claimed '" << scope << "'\n";
            return 0;
        }

        if (sub == "list") {
            auto held = core::lastPerScope(readAll());
            int shown = 0;
            for (const auto& l : held) {
                if (now - l.heartbeat_at > TTL) continue;   // stale / released
                std::cout << "  " << l.scope << "  (held by " << l.host
                          << ", " << (now - l.heartbeat_at) << "s ago)\n";
                ++shown;
            }
            if (shown == 0) std::cout << "(no live locks)\n";
            return 0;
        }

        usage();
        return sub.empty() ? 0 : 1;
    }
};

ICMG_REGISTER_COMMAND("lock", LockCommand);

}  // namespace icmg::cli
