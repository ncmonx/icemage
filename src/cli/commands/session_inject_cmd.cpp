// v1.15.0: `icmg session-inject` — combined SessionStart RPC.
//
// Replaces 3 sequential SessionStart hook scripts (sayless + context +
// wakeup) with single in-process operation. Emits one
// {"hookSpecificOutput":{"hookEventName":"SessionStart","additionalContext":"..."}}
// envelope containing concatenated content from:
//   - icmg sayless directive (if sayless.flag exists)
//   - icmg context-node match hot tier + skill manifest + focus + rules
//   - icmg wake-up briefing (decisions + fixes + memoirs)
//
// Saves 3 cold-spawn × ~360ms = 1080ms per session-start vs 1 IPC ~5ms.
//
// Hook integration: single .claude/hooks/icmg-session-inject.sh wrapper
// emits via `icmg hookio emit SessionStart --ctx-stdin` after `icmg
// session-inject` produces text.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/inject_dedup.hpp"
#include "../../core/turn_cache.hpp"

#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

namespace icmg::cli {

namespace {

// Run a registered command, capture its stdout into string.
// Empty string on error/missing cmd.
std::string captureCmd(const std::string& cmd_name,
                       const std::vector<std::string>& argv) {
    std::ostringstream buf;
    auto* old_cout = std::cout.rdbuf(buf.rdbuf());
    auto* old_cerr = std::cerr.rdbuf();  // swallow stderr
    std::ostringstream null_err;
    std::cerr.rdbuf(null_err.rdbuf());
    try {
        auto& reg = core::Registry<BaseCommand>::instance();
        auto cmd = reg.create(cmd_name);
        if (cmd) (void)cmd->run(argv);
    } catch (...) {}
    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);
    return buf.str();
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

}  // namespace

class SessionInjectCommand : public BaseCommand {
public:
    std::string name()        const override { return "session-inject"; }
    std::string description() const override {
        return "Combined SessionStart inject (sayless + context + wake-up)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg session-inject [--skip-sayless] [--skip-context] [--skip-wakeup]\n\n"
            "Emits concatenated SessionStart inject text to stdout.\n"
            "Replaces 3 sequential hook scripts with single in-process call.\n"
            "Pipe through `icmg hookio emit SessionStart --ctx-stdin` for envelope.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool skip_sayless = hasFlag(args, "--skip-sayless");
        bool skip_context = hasFlag(args, "--skip-context");
        bool skip_wakeup  = hasFlag(args, "--skip-wakeup");

        // v1.18.0: session boundary — reset hash-based dedup state.
        // SessionStart fires once per Claude Code session. Dedup that
        // persisted across prior session would falsely skip content
        // user expects to see on fresh session.
        core::inject_dedup::resetSession();
        core::turn_cache::resetSession();

        std::string out;

        // 1. Sayless directive (if flag set).
        if (!skip_sayless) {
            std::string c = captureCmd("compliance", {"inject"});
            c = trim(c);
            if (!c.empty()) {
                out += c;
                out += "\n\n";
            }
        }

        // 2. Context: hot context_nodes + skill manifest + focus + rules.
        if (!skip_context) {
            std::string hot   = captureCmd("context-node",
                {"match", "", "--tier", "hot", "--top", "5", "--fmt", "plain"});
            std::string skill = captureCmd("skill",  {"manifest"});
            std::string focus = captureCmd("focus",  {"inject"});
            std::string rules = captureCmd("rules",  {"inject"});
            for (auto& part : {trim(hot), trim(skill), trim(focus), trim(rules)}) {
                if (!part.empty()) {
                    if (!out.empty() && out.back() != '\n') out += "\n\n";
                    out += part;
                    out += "\n\n";
                }
            }
        }

        // 3. Wake-up briefing.
        if (!skip_wakeup) {
            std::string w = captureCmd("wake-up", {});
            w = trim(w);
            if (!w.empty()) {
                if (!out.empty() && out.back() != '\n') out += "\n\n";
                out += w;
            }
        }

        std::string trimmed = trim(out);
        std::cout << trimmed;
        return 0;
    }
};

ICMG_REGISTER_COMMAND("session-inject", SessionInjectCommand);

}  // namespace icmg::cli
