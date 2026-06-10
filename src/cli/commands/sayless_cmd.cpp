// `icmg sayless` — TWO independent directive toggles, each with its own on/off:
//   RESPONSE terseness  -> flags .icmg/sayless.flag|.off, ~/.icmg/sayless.flag
//   THINKING compression -> flags .icmg/sayless-think.flag|.off, ~/.icmg/sayless-think.flag
//
// Split (was a single flag governing both): response and thinking are now
// independent — toggle one without touching the other. Thinking directive is
// BRUTAL (symbol/abbrev) when on (emitted by the SessionStart sayless hook).
//
// Scope precedence per toggle (resolveSayless in ../sayless_resolve.hpp):
//   project OFF marker > project ON flag > global ON flag > default OFF.
// Default action scope is PROJECT; pass --global to target ~/.icmg.

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
        return "Toggle sayless directives — response terseness + thinking compression (independent)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg sayless <action> [--global]\n\n"
            "Response (terse replies):\n"
            "  on | off | level <L>     L: lite | full | ultra (default) | hyper\n\n"
            "Thinking (compress internal reasoning — BRUTAL symbol/abbrev when on):\n"
            "  thinking on | thinking off | thinking status\n\n"
            "  status                   Show BOTH response + thinking state\n\n"
            "Scope precedence per toggle: project OFF > project ON > global ON > default OFF.\n"
            "--global targets ~/.icmg. Response + thinking are independent — toggling\n"
            "one never touches the other.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];
        bool global = hasFlag(args, "--global");

        // THINKING toggle (separate flag base). --level lite|ultra|hyper sets
        // shorthand density (stored in the flag; read by the SessionStart hook).
        if (action == "thinking") {
            std::string sub = args.size() > 1 ? args[1] : "status";
            std::string lvl = flagValue(args, "--level", "ultra");
            return toggle(sub, global, "sayless-think", "THINKING", lvl);
        }

        // RESPONSE toggle (base flag) — keeps level + backward-compat.
        if (action == "on") {
            std::error_code ec;
            if (global) {
                fs::create_directories(globalFlag("sayless").parent_path(), ec);
                std::ofstream f(globalFlag("sayless")); f << "ultra\n";
                std::cout << "icmg sayless: RESPONSE ON (global, level=ultra)\n";
            } else {
                fs::create_directories(projFlag("sayless").parent_path(), ec);
                fs::remove(projOff("sayless"), ec);
                std::ofstream f(projFlag("sayless")); f << "ultra\n";
                std::cout << "icmg sayless: RESPONSE ON (this project, level=ultra)\n";
            }
            return 0;
        }
        if (action == "off") {
            std::error_code ec;
            if (global) {
                fs::remove(globalFlag("sayless"), ec);
                std::cout << "icmg sayless: RESPONSE OFF (global flag removed)\n";
            } else {
                fs::remove(projFlag("sayless"), ec);
                std::ofstream f(projOff("sayless")); f << "off\n";
                std::cout << "icmg sayless: RESPONSE OFF (this project; OFF marker set)\n";
            }
            return 0;
        }
        if (action == "status") {
            auto r = effectiveFor("sayless");
            auto t = effectiveFor("sayless-think");
            std::cout << "icmg sayless:\n"
                      << "  response: " << (r.on ? "ON" : "OFF")
                      << " (source=" << r.source
                      << (r.on ? (", level=" + r.level) : "") << ")\n"
                      << "  thinking: " << (t.on ? ("ON (brutal, level=" + t.level + ")") : std::string("OFF"))
                      << " (source=" << t.source << ")\n";
            return 0;
        }
        if (action == "level") {
            if (args.size() < 2) { std::cerr << "level requires value\n"; return 2; }
            std::string lvl = args[1];
            std::error_code ec;
            fs::path target = global ? globalFlag("sayless") : projFlag("sayless");
            fs::create_directories(target.parent_path(), ec);
            std::ofstream f(target); f << lvl << "\n";
            if (!global) fs::remove(projOff("sayless"), ec);
            std::cout << "icmg sayless: RESPONSE level=" << lvl
                      << " (" << (global ? "global" : "project") << ")\n";
            return 0;
        }

        usage();
        return 1;
    }

private:
    static fs::path homeDir() {
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (!home) home = ".";
        return fs::path(home);
    }
    static fs::path globalFlag(const std::string& base) { return homeDir() / ".icmg" / (base + ".flag"); }
    static fs::path projFlag(const std::string& base)   { return fs::path(".icmg") / (base + ".flag"); }
    static fs::path projOff(const std::string& base)    { return fs::path(".icmg") / (base + ".off");  }

    static std::string readLevel(const fs::path& flag) {
        std::ifstream f(flag);
        std::string lvl; if (f) std::getline(f, lvl);
        return lvl;
    }

    static SaylessState effectiveFor(const std::string& base) {
        bool poff = fs::exists(projOff(base));
        bool pon  = fs::exists(projFlag(base));
        bool gon  = fs::exists(globalFlag(base));
        return resolveSayless(poff, pon, gon,
                              pon ? readLevel(projFlag(base)) : "",
                              gon ? readLevel(globalFlag(base)) : "");
    }

    // Generic on/off/status for a flag base (used by `thinking`).
    int toggle(const std::string& sub, bool global,
               const std::string& base, const std::string& label,
               const std::string& onValue = "on") {
        std::error_code ec;
        std::string tag = (onValue != "on") ? (", level=" + onValue) : "";
        if (sub == "on") {
            if (global) {
                fs::create_directories(globalFlag(base).parent_path(), ec);
                std::ofstream f(globalFlag(base)); f << onValue << "\n";
                std::cout << "icmg sayless: " << label << " ON (global" << tag << ")\n";
            } else {
                fs::create_directories(projFlag(base).parent_path(), ec);
                fs::remove(projOff(base), ec);
                std::ofstream f(projFlag(base)); f << onValue << "\n";
                std::cout << "icmg sayless: " << label << " ON (this project" << tag << ")\n";
            }
            return 0;
        }
        if (sub == "off") {
            if (global) {
                fs::remove(globalFlag(base), ec);
                std::cout << "icmg sayless: " << label << " OFF (global flag removed)\n";
            } else {
                fs::remove(projFlag(base), ec);
                std::ofstream f(projOff(base)); f << "off\n";
                std::cout << "icmg sayless: " << label << " OFF (this project; OFF marker set)\n";
            }
            return 0;
        }
        if (sub == "status") {
            auto s = effectiveFor(base);
            std::cout << "icmg sayless " << label << ": " << (s.on ? "ON" : "OFF")
                      << " (source=" << s.source << ")\n";
            return 0;
        }
        std::cerr << "sayless " << label << ": unknown sub-action '" << sub
                  << "' (on|off|status)\n";
        return 2;
    }
};

ICMG_REGISTER_COMMAND("sayless", SaylessCommand);

}  // namespace icmg::cli
