// Phase 51 T2: `icmg caveman on/off/status` — toggle caveman directive
// auto-inject across Claude Code session via SessionStart hook.
//
// Flag location: ~/.icmg/caveman.flag (presence = enabled)
// Hook (icmg-caveman-prompt.sh) reads the flag at session start and emits
// JSON additionalContext with the caveman directive.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
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
        return "Toggle caveman directive auto-inject (on/off/status)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg caveman <action>\n\n"
            "Actions:\n"
            "  on             Enable caveman auto-inject (writes ~/.icmg/caveman.flag)\n"
            "  off            Disable (removes flag)\n"
            "  status         Show current state\n"
            "  level <L>      Set level: lite | full | ultra (default ultra)\n\n"
            "When ON, every Claude Code session that runs icmg-caveman-prompt.sh\n"
            "(installed by `icmg init`) injects the caveman directive as additional\n"
            "context — including thinking phases for compatible models.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];

        fs::path flag_path = flagPath();

        if (action == "on") {
            std::error_code ec;
            fs::create_directories(flag_path.parent_path(), ec);
            std::ofstream f(flag_path);
            f << "ultra\n";  // default level
            std::cout << "icmg caveman: ON (level=ultra) — flag at " << flag_path.string() << "\n"
                      << "  New Claude sessions auto-receive caveman directive.\n"
                      << "  Run `icmg init --install-hooks --force` if hook not installed.\n";
            return 0;
        }
        if (action == "off") {
            std::error_code ec;
            fs::remove(flag_path, ec);
            std::cout << "icmg caveman: OFF — flag removed.\n";
            return 0;
        }
        if (action == "status") {
            if (fs::exists(flag_path)) {
                std::string level = "ultra";
                std::ifstream f(flag_path);
                if (f) std::getline(f, level);
                std::cout << "icmg caveman: ON (level=" << level << ")\n"
                          << "  flag: " << flag_path.string() << "\n";
                // Hook installed?
                fs::path hook = fs::current_path() / ".claude" / "hooks" / "icmg-caveman-prompt.sh";
                std::cout << "  hook: " << (fs::exists(hook) ? "INSTALLED" : "MISSING (run icmg init --install-hooks --force)")
                          << "\n";
                // Last trigger.
                fs::path last = flag_path.parent_path() / "caveman-last-trigger.txt";
                if (fs::exists(last)) {
                    std::ifstream lf(last);
                    std::string ts; std::getline(lf, ts);
                    std::cout << "  last fired: " << ts << "\n";
                } else {
                    std::cout << "  last fired: (no record yet — restart Claude session)\n";
                }
            } else {
                std::cout << "icmg caveman: OFF\n"
                          << "  Enable with: icmg caveman on\n";
            }
            return 0;
        }
        if (action == "level" && args.size() >= 2) {
            std::string lvl = args[1];
            if (lvl != "lite" && lvl != "full" && lvl != "ultra") {
                std::cerr << "icmg caveman level: must be lite|full|ultra\n";
                return 1;
            }
            std::error_code ec;
            fs::create_directories(flag_path.parent_path(), ec);
            std::ofstream f(flag_path);
            f << lvl << "\n";
            std::cout << "icmg caveman: level=" << lvl << " (also turns ON)\n";
            return 0;
        }

        usage();
        return 1;
    }

private:
    static fs::path flagPath() {
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (!home) home = ".";
        return fs::path(home) / ".icmg" / "caveman.flag";
    }
};

ICMG_REGISTER_COMMAND("caveman", CavemanCommand);

} // namespace icmg::cli
