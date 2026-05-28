// Phase 55: `icmg strict on/off/status` — global enforcement toggle.
//
// When ON: ~/.icmg/strict.flag exists → init/upgrade auto-installs hooks
// in --strict-read mode (hard-deny raw Read on big non-source files,
// redirect to `icmg context`). update --apply re-runs init with strict.
//
// Goal: make Claude obey "always use icmg context" rule via harness-level
// hook block, not soft CLAUDE.md hint that the model can ignore.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

class StrictCommand : public BaseCommand {
public:
    std::string name()        const override { return "strict"; }
    std::string description() const override {
        return "Toggle hook-level rule enforcement (on/off/status)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg strict <action>\n\n"
            "Actions:\n"
            "  on        Enable strict mode globally (writes ~/.icmg/strict.flag)\n"
            "  off       Disable (removes flag)\n"
            "  status    Show current state + check active hooks in CWD\n"
            "  report    Show recent native-tool denials (~/.icmg/strict-denials.jsonl)\n\n"
            "When ON, `icmg init` and `icmg update --apply` auto-install\n"
            "PreToolUse:Read hook in HARD-DENY mode for non-source files >20KB.\n"
            "Claude is forced to use `icmg context <file>` instead of raw Read.\n"
            "Source-code extensions (cs/ts/py/cpp/...) remain readable directly.\n\n"
            "Re-run `icmg init --install-hooks --force` in each project to apply.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];
        fs::path flag = flagPath();

        if (action == "on") {
            std::error_code ec;
            fs::create_directories(flag.parent_path(), ec);
            std::ofstream f(flag);
            f << "1\n";
            std::cout << "icmg strict: ON — flag at " << flag.string() << "\n"
                      << "  Future `icmg init` + `icmg update --apply` install strict hooks.\n"
                      << "  Apply now: `icmg init --install-hooks --strict-read --force`\n";
            return 0;
        }
        if (action == "off") {
            std::error_code ec;
            fs::remove(flag, ec);
            std::cout << "icmg strict: OFF — flag removed.\n"
                      << "  Existing strict hooks remain until reinstalled without --strict-read.\n";
            return 0;
        }
        if (action == "status") {
            bool on = fs::exists(flag);
            std::cout << "icmg strict: " << (on ? "ON" : "OFF") << "\n"
                      << "  global flag: " << flag.string() << (on ? " (present)" : " (absent)") << "\n";
            // Check current project hooks.
            fs::path settings = fs::current_path() / ".claude" / "settings.local.json";
            if (fs::exists(settings)) {
                std::ifstream sf(settings);
                std::string content((std::istreambuf_iterator<char>(sf)), {});
                bool has_strict = content.find("ICMG_SHRINK_STRICT=1") != std::string::npos;
                std::cout << "  project hooks: " << (has_strict ? "STRICT installed" : "non-strict (run init --strict-read)")
                          << "\n";
            } else {
                std::cout << "  project hooks: (no .claude/settings.local.json — run `icmg init`)\n";
            }
            if (!on) std::cout << "  Enable with: icmg strict on\n";
            return 0;
        }
        if (action == "report") {
            // v1.56 T4 (G2): aggregate the strict-denials JSONL written by the
            // bash-rewrite / bash-strict hooks. Each line is a small JSON
            // object {"ts":...,"hook":...,"cmd":...,"reason":...}. We do a
            // cheap field-extract (no full JSON parse) and tally by hook.
            fs::path log = flagPath().parent_path() / "strict-denials.jsonl";
            if (!fs::exists(log)) {
                std::cout << "icmg strict report: no denials logged yet ("
                          << log.string() << " absent)\n";
                return 0;
            }
            std::ifstream lf(log);
            std::string line;
            int total = 0;
            std::map<std::string, int> by_hook;
            std::vector<std::string> recent;  // keep last 10 cmd snippets
            auto field = [](const std::string& s, const std::string& key) -> std::string {
                std::string pat = "\"" + key + "\":\"";
                auto p = s.find(pat);
                if (p == std::string::npos) return {};
                p += pat.size();
                auto e = s.find('"', p);
                while (e != std::string::npos && s[e - 1] == '\\')
                    e = s.find('"', e + 1);
                if (e == std::string::npos) return {};
                return s.substr(p, e - p);
            };
            while (std::getline(lf, line)) {
                if (line.empty()) continue;
                ++total;
                std::string hook = field(line, "hook");
                if (hook.empty()) hook = "(unknown)";
                ++by_hook[hook];
                std::string cmd = field(line, "cmd");
                if (!cmd.empty()) {
                    if (recent.size() >= 10) recent.erase(recent.begin());
                    recent.push_back(cmd);
                }
            }
            std::cout << "icmg strict report — " << total
                      << " native-tool denial(s) logged\n"
                      << "  log: " << log.string() << "\n\n"
                      << "By hook:\n";
            for (const auto& [h, n] : by_hook)
                std::cout << "  " << h << ": " << n << "\n";
            if (!recent.empty()) {
                std::cout << "\nMost recent (up to 10):\n";
                for (const auto& c : recent) {
                    std::string snip = c.size() > 80 ? c.substr(0, 77) + "..." : c;
                    std::cout << "  - " << snip << "\n";
                }
            }
            std::cout << "\nTip: each denial = a native call that had an icmg "
                         "equivalent. RAW=1 prefix bypasses when truly needed.\n";
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
        return fs::path(home) / ".icmg" / "strict.flag";
    }
};

ICMG_REGISTER_COMMAND("strict", StrictCommand);

} // namespace icmg::cli
