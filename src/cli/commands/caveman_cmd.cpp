// Phase 51 T2 + v1.66 per-project: `icmg caveman on/off/status/level`.
//
// Scope precedence (resolveCaveman in ../caveman_resolve.hpp):
//   project OFF marker (.icmg/caveman.off) > project ON (.icmg/caveman.flag)
//   > global ON (~/.icmg/caveman.flag) > default OFF.
// Default action scope is PROJECT; pass --global to target ~/.icmg.
// Lets each project be independent (new project defaults OFF even if global
// is on; opt a project ON locally without touching global).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../caveman_resolve.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace icmg::cli {

class CavemanCommand : public BaseCommand {
public:
    std::string name()        const override { return "caveman"; }
    std::string description() const override {
        return "Toggle caveman directive auto-inject (per-project or --global)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg caveman <action> [--global]\n\n"
            "Actions:\n"
            "  on             Enable (default: this project; --global = all projects)\n"
            "  off            Disable (project: writes OFF marker overriding global)\n"
            "  status         Show effective state + which scope decided it\n"
            "  level <L>      Set level: lite | full | ultra (default ultra)\n\n"
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
                std::cout << "icmg caveman: ON (global, level=ultra)\n";
            } else {
                fs::create_directories(projFlag().parent_path(), ec);
                fs::remove(projOff(), ec);               // clear OFF marker
                std::ofstream f(projFlag()); f << "ultra\n";
                std::cout << "icmg caveman: ON (this project, level=ultra)\n";
            }
            return 0;
        }
        if (action == "off") {
            std::error_code ec;
            if (global) {
                fs::remove(globalFlag(), ec);
                std::cout << "icmg caveman: OFF (global flag removed)\n";
            } else {
                fs::create_directories(projOff().parent_path(), ec);
                fs::remove(projFlag(), ec);              // clear project ON
                std::ofstream f(projOff()); f << "off\n"; // OFF marker overrides global
                std::cout << "icmg caveman: OFF (this project — overrides global)\n";
            }
            return 0;
        }
        if (action == "status") {
            auto st = effective();
            if (st.on)
                std::cout << "icmg caveman: ON (level=" << st.level
                          << ", source=" << st.source << ")\n";
            else
                std::cout << "icmg caveman: OFF (source=" << st.source << ")\n";
            std::cout << "  project flag: " << projFlag().string()
                      << (fs::exists(projFlag()) ? " [present]" : "") << "\n"
                      << "  project off : " << projOff().string()
                      << (fs::exists(projOff()) ? " [present]" : "") << "\n"
                      << "  global flag : " << globalFlag().string()
                      << (fs::exists(globalFlag()) ? " [present]" : "") << "\n";
            return 0;
        }
        if (action == "level" && args.size() >= 2) {
            std::string lvl = args[1];
            std::error_code ec;
            fs::path target = global ? globalFlag() : projFlag();
            fs::create_directories(target.parent_path(), ec);
            if (!global) fs::remove(projOff(), ec);
            std::ofstream f(target); f << lvl << "\n";
            std::cout << "icmg caveman: level=" << lvl << " (also ON, "
                      << (global ? "global" : "this project") << ")\n";
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
        return fs::path(home) / ".icmg" / "caveman.flag";
    }
    static fs::path projFlag() { return fs::path(".icmg") / "caveman.flag"; }
    static fs::path projOff()  { return fs::path(".icmg") / "caveman.off";  }

    static std::string readLevel(const fs::path& flag) {
        std::ifstream f(flag);
        std::string lvl; if (f) std::getline(f, lvl);
        return lvl;
    }

    static CavemanState effective() {
        bool poff = fs::exists(projOff());
        bool pon  = fs::exists(projFlag());
        bool gon  = fs::exists(globalFlag());
        return resolveCaveman(poff, pon, gon,
                              pon ? readLevel(projFlag()) : "",
                              gon ? readLevel(globalFlag()) : "");
    }
};

ICMG_REGISTER_COMMAND("caveman", CavemanCommand);

}  // namespace icmg::cli
