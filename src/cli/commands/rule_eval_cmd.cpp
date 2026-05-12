// icmg rule-eval — PreToolUse hook client: check tool event against enforcement rules.
//
// Usage:
//   icmg rule-eval --tool Read --file /path/to/file.cpp
//   icmg rule-eval --tool Read --file /path/to/file.cpp --lines 823
//   icmg rule-eval --tool PING
//
// Output (stdout):
//   ALLOW                   → tool call permitted
//   WARN <message>          → allowed but icmg alternative suggested
//   BLOCK <message>         → emits Claude Code hook deny JSON to stdout
//
// Exit codes: 0=ALLOW/WARN, 2=BLOCK (hook deny), 1=error

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../daemon/rule_daemon_client.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

using namespace icmg::daemon;
using nlohmann::json;

namespace icmg::cli {

class RuleEvalCommand : public BaseCommand {
public:
    std::string name()        const override { return "rule-eval"; }
    std::string description() const override { return "Check tool event against enforcement rules (PreToolUse hook client)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg rule-eval --tool <NAME> [--file PATH] [--lines N]\n\n"
            "Check one tool event against the rule-daemon enforcement rules.\n"
            "Used by the PreToolUse hook; fail-open (ALLOW) if daemon is down.\n\n"
            "Options:\n"
            "  --tool  PING|RELOAD|Read|Glob|Grep|...  Tool name\n"
            "  --file  PATH                             File path (Read/Glob/Grep)\n"
            "  --lines N                                Known line count (skip file scan)\n"
            "  --json                                   Always emit JSON output\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }

        std::string tool, file;
        int lines = 0;
        bool json_out = hasFlag(args, "--json");

        for (size_t i = 0; i < args.size(); ++i) {
            if      (args[i] == "--tool"  && i+1 < args.size()) tool  = args[++i];
            else if (args[i] == "--file"  && i+1 < args.size()) file  = args[++i];
            else if (args[i] == "--lines" && i+1 < args.size())
                try { lines = std::stoi(args[++i]); } catch (...) {}
        }

        if (tool.empty()) {
            std::cerr << "rule-eval: --tool required\n";
            return 1;
        }

        // Special commands
        if (tool == "PING") {
            bool ok = RuleDaemonClient::ping();
            std::cout << (ok ? "PONG" : "DAEMON_DOWN") << "\n";
            return ok ? 0 : 1;
        }

        auto result = RuleDaemonClient::check(tool, file, lines);

        if (result.action == "ALLOW") {
            if (json_out) std::cout << "{\"action\":\"ALLOW\"}\n";
            return 0;
        }

        if (result.action == "WARN") {
            // Emit additionalContext suggestion — doesn't block the tool
            json out;
            out["hookSpecificOutput"]["hookEventName"]   = "PreToolUse";
            out["hookSpecificOutput"]["additionalContext"] =
                "icmg hint [" + tool + "]: " + result.message;
            std::cout << out.dump() << "\n";
            return 0;
        }

        if (result.action == "BLOCK") {
            // Emit permissionDecision deny — Claude Code blocks the tool call
            json out;
            out["hookSpecificOutput"]["hookEventName"]           = "PreToolUse";
            out["hookSpecificOutput"]["permissionDecision"]       = "deny";
            out["hookSpecificOutput"]["permissionDecisionReason"] = result.message;
            std::cout << out.dump() << "\n";
            return 2;  // non-zero exit → hook deny
        }

        return 0;
    }
};

ICMG_REGISTER_COMMAND("rule-eval", RuleEvalCommand);

} // namespace icmg::cli
