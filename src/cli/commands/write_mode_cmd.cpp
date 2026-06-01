// v1.25.0 (W2): `icmg write-mode` — toggle compressed-write protocol.
//
// When enabled, the PreToolUse:Write hook injects a rule telling AI to emit
// content as compact diff/template/glossary, and PostToolUse:Write expands
// it before disk write. See `src/compress/write_expander.{hpp,cpp}` for the
// expander side and `init_cmd::COMPRESSED_WRITE_RULE_SH` for the rule script.
//
// Flag file: `~/.icmg/write-mode.flag` containing one of: diff|template|glossary|auto|off
// Default (no flag) = off (opt-in, user-confirmed in v1.25.0 design Qs).

#include "../base_command.hpp"
#include "../../core/registry.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

fs::path flagPath() {
    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    return fs::path(home ? home : ".") / ".icmg" / "write-mode.flag";
}

bool validMode(const std::string& m) {
    return m == "diff" || m == "template" || m == "glossary"
        || m == "auto" || m == "off";
}

}  // namespace

class WriteModeCommand : public BaseCommand {
public:
    std::string name()        const override { return "write-mode"; }
    std::string description() const override {
        return "Toggle compressed-write protocol (v1.25.0 W2): AI emits "
               "diff/template/glossary; icmg expands before disk write.";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg write-mode <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  on [--mode diff|template|glossary|auto]   Enable. Default --mode auto.\n"
            "  off                                       Disable (remove flag).\n"
            "  status                                    Print current mode.\n\n"
            "Modes:\n"
            "  diff       AI emits unified diff against existing file's SHA.\n"
            "             Typical saving: 70-95% for incremental edits.\n"
            "  template   AI emits {template-id, slot-values} JSON.\n"
            "             Typical saving: 80-95% when pattern matches.\n"
            "  glossary   AI emits text with short tokens (`<%c1%>`).\n"
            "             Typical saving: 15-30% for any text.\n"
            "  auto       Per-file: existing -> diff, pattern-match -> template, else -> glossary.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h") || args.empty()) {
            usage(); return 0;
        }
        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        if (sub == "on") {
            std::string mode = flagValue(rest, "--mode", "auto");
            if (!validMode(mode) || mode == "off") {
                std::cerr << "write-mode on: bad --mode '" << mode << "'\n";
                return 1;
            }
            auto p = flagPath();
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream(p) << mode;
            std::cout << "write-mode: on (mode=" << mode << ")\n"
                      << "  flag: " << p.string() << "\n"
                      << "  Restart your AI agent so the PreToolUse:Write hook "
                         "picks up the new mode.\n";
            return 0;
        }

        if (sub == "off") {
            std::error_code ec;
            fs::remove(flagPath(), ec);
            std::cout << "write-mode: off (flag removed)\n";
            return 0;
        }

        if (sub == "status") {
            auto p = flagPath();
            if (!fs::exists(p)) {
                std::cout << "write-mode: off\n";
                return 0;
            }
            std::ifstream f(p);
            std::string mode;
            f >> mode;
            if (mode.empty()) mode = "auto";
            std::cout << "write-mode: on\n  mode: " << mode << "\n  flag: "
                      << p.string() << "\n";
            return 0;
        }

        std::cerr << "write-mode: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("write-mode", WriteModeCommand);

}  // namespace icmg::cli
