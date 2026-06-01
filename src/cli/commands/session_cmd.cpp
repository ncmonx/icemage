// #1130 T1: `icmg session` — cross-session task awareness.
// Writes/reads ~/.icmg/active-work.json so icmg wake-up can show active tasks
// from other concurrent sessions on the same machine.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"          // v2.0.0 Phase 4: agent_leases DB
#include "../../core/agent_lease.hpp" // v2.0.0 Phase 4: resolveClaim
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

// ~/.icmg/active-work.json path
static fs::path activeWorkPath() {
    auto& cfg = core::Config::instance();
    return fs::path(cfg.globalDbPath()).parent_path() / "active-work.json";
}

static json loadActiveWork() {
    auto p = activeWorkPath();
    if (!fs::exists(p)) return {{"sessions", json::array()}};
    try {
        std::ifstream f(p);
        return json::parse(f);
    } catch (...) {
        return {{"sessions", json::array()}};
    }
}

static void saveActiveWork(const json& j) {
    auto p = activeWorkPath();
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << j.dump(2) << "\n";
}

static std::string hostName() {
#ifdef _WIN32
    char buf[256]; DWORD n = sizeof(buf);
    if (GetComputerNameA(buf, &n)) return std::string(buf, n);
    return "win";
#else
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0) { buf[255] = 0; return buf; }
    return "host";
#endif
}

// v2.0.0 Phase 4: stable lease owner id. Each icmg CLI invocation is a fresh OS
// process, so OS pid cannot identify an AGENT across claim/release calls. Agents
// set ICMG_AGENT_ID (any string) for a stable identity; fallback = OS pid.
static int64_t agentOwnerId();

static int64_t currentPid() {
#ifdef _WIN32
    return (int64_t)GetCurrentProcessId();
#else
    return (int64_t)getpid();
#endif
}

static int64_t agentOwnerId() {
    const char* a = std::getenv("ICMG_AGENT_ID");
    if (a && *a) {
        // djb2 hash -> positive int64 so a string id maps to the pid column.
        unsigned long h = 5381; for (const char* p = a; *p; ++p) h = ((h << 5) + h) + (unsigned char)*p;
        return (int64_t)(h & 0x7fffffffffffffffULL);
    }
    return currentPid();
}

class SessionCommand : public BaseCommand {
public:
    std::string name()        const override { return "session"; }
    std::string description() const override { return "Cross-session task awareness (claim/clear/list)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg session <subcommand> [args]\n\n"
            "Subcommands:\n"
            "  claim <task>   Register active task in ~/.icmg/active-work.json\n"
            "  clear          Remove this process's entry\n"
            "  list           List all registered active sessions\n"
            "  claim <task> --scope S   Claim a work scope (DB lease; blocks other live agents)\n"
            "  leases         List active DB work-scope leases\n"
            "  release [--scope S]      Release this process's lease(s)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        if (sub == "claim") return runClaim(args);
        if (sub == "clear") return runClear();
        if (sub == "list")  return runList();
        if (sub == "leases") return runLeases();
        if (sub == "release") return runRelease(args);
        std::cerr << "icmg session: unknown subcommand '" << sub << "'\n";
        return 1;
    }

private:
    int runLeases() {
        try {
            core::Db gdb(core::Config::instance().globalDbPath());
            int64_t now = (int64_t)std::time(nullptr);
            bool any = false;
            gdb.query("SELECT scope,pid,host,task,heartbeat_at FROM agent_leases "
                      "ORDER BY scope", {}, [&](const core::Row& r){
                if (r.size() < 5) return;
                int64_t hb = 0; try { hb = std::stoll(r[4]); } catch (...) {}
                const char* state = (now - hb > 300) ? "stale" : "live";
                std::cout << "  [" << state << "] " << r[0]
                          << "  pid=" << r[1] << " @ " << r[2]
                          << "  " << r[3] << "\n";
                any = true;
            });
            if (!any) std::cout << "No active leases.\n";
        } catch (const std::exception& e) {
            std::cerr << "icmg session leases: " << e.what() << "\n"; return 1;
        }
        return 0;
    }

    int runRelease(const std::vector<std::string>& args) {
        try {
            core::Db gdb(core::Config::instance().globalDbPath());
            int64_t pid = agentOwnerId();
            std::string host = hostName();
            std::string scope = flagValue(args, "--scope");
            if (scope.empty()) {
                gdb.run("DELETE FROM agent_leases WHERE pid=? AND host=?",
                        {std::to_string(pid), host});
                std::cout << "icmg session: released all leases for pid=" << pid << "\n";
            } else {
                gdb.run("DELETE FROM agent_leases WHERE scope=? AND pid=? AND host=?",
                        {scope, std::to_string(pid), host});
                std::cout << "icmg session: released scope '" << scope << "'\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "icmg session release: " << e.what() << "\n"; return 1;
        }
        return 0;
    }

    int runClaim(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            std::cerr << "icmg session claim: task description required\n";
            return 1;
        }
        std::string task;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i].rfind("--", 0) == 0) break;  // stop at flags (e.g. --scope)
            if (!task.empty()) task += " ";
            task += args[i];
        }
        auto j = loadActiveWork();
        int64_t pid = currentPid();
        int64_t now = (int64_t)std::time(nullptr);
        auto& sessions = j["sessions"];
        // Remove stale entry for this PID.
        sessions.erase(
            std::remove_if(sessions.begin(), sessions.end(),
                [pid](const json& s){ return s.value("pid", int64_t{0}) == pid; }),
            sessions.end());
        sessions.push_back({
            {"pid",        pid},
            {"task",       task},
            {"started_at", now},
            {"last_seen",  now}
        });
        saveActiveWork(j);
        std::cout << "icmg session: claimed '" << task << "' (pid=" << pid << ")\n";

        // v2.0.0 Phase 4: optional DB work-scope lease (conflict-free across agents).
        std::string scope = flagValue(args, "--scope");
        if (!scope.empty()) {
            try {
                core::Db gdb(core::Config::instance().globalDbPath());
                gdb.run("CREATE TABLE IF NOT EXISTS agent_leases ("
                        "id INTEGER PRIMARY KEY AUTOINCREMENT, scope TEXT NOT NULL, "
                        "pid INTEGER NOT NULL, host TEXT NOT NULL DEFAULT '', "
                        "task TEXT NOT NULL DEFAULT '', "
                        "claimed_at INTEGER NOT NULL DEFAULT (strftime('%s','now')), "
                        "heartbeat_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
                std::string host = hostName();
                std::vector<core::AgentLease> existing;
                gdb.query("SELECT scope,pid,host,heartbeat_at FROM agent_leases WHERE scope=?",
                          {scope}, [&](const core::Row& r){
                    if (r.size() >= 4) {
                        core::AgentLease l; l.scope = r[0]; l.host = r[2];
                        try { l.pid = std::stoll(r[1]); } catch (...) {}
                        try { l.heartbeat_at = std::stoll(r[3]); } catch (...) {}
                        existing.push_back(l);
                    }
                });
                int64_t owner = agentOwnerId();
                auto res = core::resolveClaim(existing, scope, owner, host, now, 300);
                if (!res.granted) {
                    std::cerr << "icmg session: scope '" << scope << "' is held by pid="
                              << res.conflict_pid << " @ " << res.conflict_host
                              << " -- not leased (wait or release theirs)\n";
                    return 1;
                }
                gdb.run("DELETE FROM agent_leases WHERE scope=? AND pid=? AND host=?",
                        {scope, std::to_string(owner), host});
                gdb.run("INSERT INTO agent_leases(scope,pid,host,task,claimed_at,heartbeat_at)"
                        " VALUES(?,?,?,?,?,?)",
                        {scope, std::to_string(owner), host, task,
                         std::to_string(now), std::to_string(now)});
                std::cout << "icmg session: leased scope '" << scope << "' (pid=" << pid << ")\n";
            } catch (const std::exception& e) {
                std::cerr << "icmg session: lease failed: " << e.what() << "\n";
                return 1;
            }
        }
        return 0;
    }

    int runClear() {
        auto j = loadActiveWork();
        int64_t pid = currentPid();
        auto& sessions = j["sessions"];
        size_t before = sessions.size();
        sessions.erase(
            std::remove_if(sessions.begin(), sessions.end(),
                [pid](const json& s){ return s.value("pid", int64_t{0}) == pid; }),
            sessions.end());
        saveActiveWork(j);
        std::cout << "icmg session: cleared "
                  << (before - sessions.size()) << " entr"
                  << (before - sessions.size() == 1 ? "y" : "ies") << "\n";
        return 0;
    }

    int runList() {
        auto j = loadActiveWork();
        const auto& sessions = j["sessions"];
        if (sessions.empty()) {
            std::cout << "(no active sessions)\n";
            return 0;
        }
        for (const auto& s : sessions) {
            std::cout << "  pid=" << s.value("pid", int64_t{0})
                      << "  " << s.value("task", std::string{}) << "\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("session", SessionCommand);

} // namespace icmg::cli
