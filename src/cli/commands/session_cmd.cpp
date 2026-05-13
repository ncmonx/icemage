// #1130 T1: `icmg session` — cross-session task awareness.
// Writes/reads ~/.icmg/active-work.json so icmg wake-up can show active tasks
// from other concurrent sessions on the same machine.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <ctime>
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

static int64_t currentPid() {
#ifdef _WIN32
    return (int64_t)GetCurrentProcessId();
#else
    return (int64_t)getpid();
#endif
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
            "  list           List all registered active sessions\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        if (sub == "claim") return runClaim(args);
        if (sub == "clear") return runClear();
        if (sub == "list")  return runList();
        std::cerr << "icmg session: unknown subcommand '" << sub << "'\n";
        return 1;
    }

private:
    int runClaim(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            std::cerr << "icmg session claim: task description required\n";
            return 1;
        }
        std::string task;
        for (size_t i = 1; i < args.size(); ++i) {
            if (i > 1) task += " ";
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
