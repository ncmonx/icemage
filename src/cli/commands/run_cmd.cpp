#include "../base_command.hpp"
#include "../run_args.hpp"   // v1.70.0 #178
#include <cstdio>
#include <cstdlib>
#if defined(_WIN32)
#  include <io.h>
#else
#  include <unistd.h>
#endif
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../tkil/tkil.hpp"
#include <iostream>
#include <sstream>
#include <cctype>
#include <vector>
#include <string>

namespace icmg::cli {

namespace {

// Returns true + sets reason if the arg list looks like a destructive operation.
bool isDestructiveOp(const std::vector<std::string>& argv, std::string& reason) {
    std::string joined;
    for (auto& a : argv) { joined += a; joined += " "; }
    std::string lc = joined;
    for (char& c : lc) c = (char)std::tolower((unsigned char)c);

    bool has_rm = lc.find("rm ") != std::string::npos;
    if (has_rm && (lc.find("-rf") != std::string::npos || lc.find("-fr") != std::string::npos
                || lc.find(" -r ") != std::string::npos || lc.find(" -f ") != std::string::npos))
        { reason = "rm with -r/-f"; return true; }

    if (lc.find("remove-item") != std::string::npos)
        { reason = "Remove-Item"; return true; }

    if (lc.find("rmdir") != std::string::npos && lc.find("/s") != std::string::npos)
        { reason = "rmdir /s"; return true; }

    if (lc.find("delete from") != std::string::npos) { reason = "DELETE FROM";    return true; }
    if (lc.find("drop table")  != std::string::npos) { reason = "DROP TABLE";     return true; }
    if (lc.find("drop database")!= std::string::npos){ reason = "DROP DATABASE";  return true; }
    if (lc.find("drop schema") != std::string::npos) { reason = "DROP SCHEMA";    return true; }
    if (lc.find("truncate ")   != std::string::npos) { reason = "TRUNCATE";       return true; }

    if (lc.find("git push") != std::string::npos && lc.find("--force") != std::string::npos)
        { reason = "git push --force"; return true; }
    if (lc.find("git reset") != std::string::npos && lc.find("--hard") != std::string::npos)
        { reason = "git reset --hard"; return true; }
    if (lc.find("git clean") != std::string::npos && lc.find("-f") != std::string::npos)
        { reason = "git clean -f"; return true; }

    return false;
}

// Returns true if every non-flag arg in argv targets a known-safe directory.
// Safe dirs are regenerable (build output, temp, caches) — no user data lost.
bool targetsSafeDir(const std::vector<std::string>& argv) {
    static const std::string safe[] = {
        "build/", "build\\", ".icmg/", ".icmg\\",
        "tmp/", "tmp\\", "temp/", "temp\\",
        "node_modules/", "node_modules\\", "__pycache__", ".cache/"
    };
    for (auto& a : argv) {
        if (a.empty() || a[0] == '-') continue;
        bool ok = false;
        for (auto& s : safe) if (a.find(s) != std::string::npos) { ok = true; break; }
        if (!ok) return false;
    }
    return true;
}

} // anonymous namespace

class RunCommand : public BaseCommand {
public:
    std::string name()        const override { return "run"; }
    std::string description() const override { return "Run command with smart output filter"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg run [options] <command...>\n\n"
            "Options:\n"
            "  --raw        No filtering — show raw output\n"
            "  --json       JSON output with metadata\n"
            "  --dry-run    Show what would be filtered without executing\n"
            "  --stream     Streaming filter (real-time output)\n"
            "  --timeout N  Execution timeout in ms (default: 60000)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        RunArgs ra = parseRunArgs(args);   // v1.70.0 #178: leading-flag-only + "--" passthrough
        bool raw = ra.raw, json_out = ra.json_out, dry_run = ra.dry_run,
             stream = ra.stream, yes = ra.yes, ultra = ra.ultra;
        if (const char* e = std::getenv("ICMG_TKIL_ULTRA"); e && *e && *e != '0')
            ultra = true;                                                         // env opt-in
        std::vector<std::string> cmd_args = ra.cmd_args;  // raw child tokens
        std::string command = ra.command;                 // quoted child command line
        if (command.empty()) { std::cerr << "icmg run: requires <command>\n"; return 1; }

        // Destructive-op guard: prompt before executing dangerous commands
        // unless --yes/-y is passed or the target is a known-safe directory.
        std::string dest_reason;
        bool assume_yes = [] { const char* e = std::getenv("ICMG_ASSUME_YES");
                               return e && *e && *e != '0'; }();
#if defined(_WIN32)
        bool stdin_tty = _isatty(_fileno(stdin)) != 0;
#else
        bool stdin_tty = isatty(fileno(stdin)) != 0;
#endif
        bool is_dest = isDestructiveOp(cmd_args, dest_reason);
        auto dd = destructiveDecision(yes, assume_yes, is_dest,
                                      targetsSafeDir(cmd_args), stdin_tty);
        if (dd == DestructiveDecision::Deny) {
            // v1.74 #184: non-interactive stdin -> never block on a prompt
            // (hangs scripts/agents). Auto-deny; opt in via --yes / env.
            std::cerr << "[icmg run] destructive op refused (" << dest_reason
                      << ") in non-interactive context. Re-run with --yes or set "
                         "ICMG_ASSUME_YES=1 to allow.\n";
            return 130;
        }
        if (dd == DestructiveDecision::Prompt) {
            std::cerr << "[WARN] Destructive operation detected: " << dest_reason << "\n"
                      << "  Command: " << command << "\n"
                      << "  Proceed? [y/N] ";
            std::string ans;
            std::getline(std::cin, ans);
            if (ans != "y" && ans != "Y") {
                std::cerr << "  [aborted]\n";
                return 130;
            }
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        tkil::Tkil executor(db);

        return executor.runFiltered(command, raw, json_out, dry_run, stream, ultra);
    }
};

ICMG_REGISTER_COMMAND("run", RunCommand);

} // namespace icmg::cli
