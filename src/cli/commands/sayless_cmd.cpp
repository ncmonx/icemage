// v1.78.1: `icmg sayless on/off/status/level` (renamed from caveman; Phase 51 T2 + v1.66 per-project).
//
// Scope precedence (resolveSayless in ../sayless_resolve.hpp):
//   project OFF marker (.icmg/sayless.off) > project ON (.icmg/sayless.flag)
//   > global ON (~/.icmg/sayless.flag) > default OFF.
// Default action scope is PROJECT; pass --global to target ~/.icmg.
// Lets each project be independent (new project defaults OFF even if global
// is on; opt a project ON locally without touching global).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../sayless_resolve.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace icmg::cli {

class SaylessCommand : public BaseCommand {
public:
    std::string name()        const override { return "sayless"; }
    std::string description() const override {
        return "Toggle sayless directive auto-inject (per-project or --global)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg sayless <action> [--global]\n\n"
            "Actions:\n"
            "  on             Enable (default: this project; --global = all projects)\n"
            "  off            Disable (project: writes OFF marker overriding global)\n"
            "  status         Show effective state + which scope decided it\n"
            "  level <L>      Set level: lite | full | ultra (default) | hyper\n\n"
            "Scope precedence: project OFF > project ON > global ON > default OFF.\n"
            "A new project defaults OFF even if global is ON.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];
        bool global = hasFlag(args, "--global");

        if (action == "on") {
            std::error_code ec;
            if (global) {
                fs::create_directories(globalFlag().parent_path(), ec);
                std::ofstream f(globalFlag()); f << "ultra\n";
                std::cout << "icmg sayless: ON (global, level=ultra)\n";
            } else {
                fs::create_directories(projFlag().parent_path(), ec);
                fs::remove(projOff(), ec);
                std::ofstream f(projFlag()); f << "ultra\n";
                std::cout << "icmg sayless: ON (this project, level=ultra)\n";
            }
            return 0;
        }
        if (action == "off") {
            std::error_code ec;
            if (global) {
                fs::remove(globalFlag(), ec);
                std::cout << "icmg sayless: OFF (global flag removed)\n";
            } else {
                fs::remove(projFlag(), ec);
                std::ofstream f(projOff()); f << "off\n";
                std::cout << "icmg sayless: OFF (this project; OFF marker set)\n";
            }
            return 0;
        }
        if (action == "status") {
            auto s = effective();
            std::cout << "icmg sayless: " << (s.on ? "ON" : "OFF")
                      << " (source=" << s.source
                      << (s.on ? (", level=" + s.level) : "")
                      << ")\n";
            return 0;
        }
        if (action == "level") {
            if (args.size() < 2) { std::cerr << "level requires value\n"; return 2; }
            std::string lvl = args[1];
            std::error_code ec;
            fs::path target = global ? globalFlag() : projFlag();
            fs::create_directories(target.parent_path(), ec);
            std::ofstream f(target); f << lvl << "\n";
            if (!global) fs::remove(projOff(), ec);
            std::cout << "icmg sayless: level=" << lvl
                      << " (" << (global ? "global" : "project") << ")\n";
            return 0;
        }

        usage();
        return 1;
    }

private:
    static fs::path globalFlag() {
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (!home) home = ".";
        return fs::path(home) / ".icmg" / "sayless.flag";
    }
    static fs::path projFlag() { return fs::path(".icmg") / "sayless.flag"; }
    static fs::path projOff()  { return fs::path(".icmg") / "sayless.off";  }

    static std::string readLevel(const fs::path& flag) {
        std::ifstream f(flag);
        std::string lvl; if (f) std::getline(f, lvl);
        return lvl;
    }

    static SaylessState effective() {
        bool poff = fs::exists(projOff());
        bool pon  = fs::exists(projFlag());
        bool gon  = fs::exists(globalFlag());
        return resolveSayless(poff, pon, gon,
                              pon ? readLevel(projFlag()) : "",
                              gon ? readLevel(globalFlag()) : "");
    }
};

ICMG_REGISTER_COMMAND("sayless", SaylessCommand);

}  // namespace icmg::cli
