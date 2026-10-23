#include "../base_command.hpp"
#include "../run_args.hpp"   // v1.70.0 #178
#include "../txn_guard.hpp"  // v2.26.0 transactional run (lite)
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
#include "../../core/cmd_densify.hpp"
#include "../../core/exec_utils.hpp"
#include "../../tkil/tkil.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cctype>
#include <ctime>
#include <vector>
#include <string>

namespace icmg::cli {

namespace {

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

// v2.26.0: transactional run (lite). Git-tree snapshot before the command;
// afterwards the change set is reviewable / abortable. Tracked files only --
// see txn_guard.hpp for the honesty note.
namespace txn {

inline std::string shQuiet(const std::string& sh_cmd, int timeout_ms = 60000) {
    auto res = core::safeExecShell(sh_cmd, true, timeout_ms);
    if (res.exit_code != 0) return "";
    std::string out = res.out;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return out;
}

// Snapshot the working tree into a git tree object via a TEMP index
// (never touches the real index). "" on failure (e.g. not a git repo).
inline std::string snapshotTree() {
    std::error_code ec;
    std::filesystem::create_directories(".icmg", ec);
    return shQuiet("GIT_INDEX_FILE=.icmg/txn.index git add -A -- . ':(exclude).icmg' >/dev/null 2>&1 && "
                   "GIT_INDEX_FILE=.icmg/txn.index git write-tree");
}

inline cli::TxnState loadState() {
    std::ifstream f(cli::txnStatePathFor("."));
    if (!f) return {};
    std::stringstream buf; buf << f.rdbuf();
    return cli::parseTxnState(buf.str());
}

inline void clearState() {
    std::error_code ec;
    std::filesystem::remove(cli::txnStatePathFor("."), ec);
    std::filesystem::remove(".icmg/txn.index", ec);
}

inline int begin() {
    std::string tree = snapshotTree();
    if (tree.empty()) {
        std::cerr << "[icmg run --txn] snapshot failed (not a git repo?)\n";
        return 1;
    }
    cli::TxnState s; s.tree = tree;
    s.ts = static_cast<std::int64_t>(std::time(nullptr));
    std::ofstream f(cli::txnStatePathFor("."));
    f << cli::serializeTxnState(s) << "\n";
    return 0;
}

inline int showDiff() {
    auto s = loadState();
    if (s.tree.empty()) { std::cerr << "[icmg run] no open txn\n"; return 1; }
    auto res = core::safeExecShell(
        "git diff --stat " + s.tree + " -- . ':(exclude).icmg'", true, 60000);
    std::cout << (res.out.empty() ? "(no tracked changes since txn begin)\n" : res.out);
    std::cout << "[txn " << s.tree.substr(0, 12) << "] review: icmg run --txn-diff | "
                 "accept: icmg run --txn-commit | revert: icmg run --txn-abort\n";
    std::cout.flush();   // non-tty stdout may be fully buffered under the executor
    return 0;
}

inline int commit() {
    auto s = loadState();
    if (s.tree.empty()) { std::cerr << "[icmg run] no open txn\n"; return 1; }
    clearState();
    std::cout << "[txn] committed (changes kept), baseline " << s.tree.substr(0, 12) << " dropped\n";
    return 0;
}

inline int abortTxn() {
    auto s = loadState();
    if (s.tree.empty()) { std::cerr << "[icmg run] no open txn\n"; return 1; }
    auto res = core::safeExecShell(
        "git restore --source=" + s.tree + " --worktree -- . ':(exclude).icmg'", true, 120000);
    if (res.exit_code != 0) {
        std::cerr << "[txn] restore failed: " << res.out << "\n";
        return 1;
    }
    // Honesty: list files that appeared after the baseline (not auto-deleted).
    std::string extra = shQuiet("git ls-files --others --exclude-standard -- . ':(exclude).icmg'");
    clearState();
    std::cout << "[txn] reverted tracked files to baseline " << s.tree.substr(0, 12) << "\n";
    if (!extra.empty())
        std::cout << "[txn] NOTE: untracked files left in place (review manually):\n"
                  << extra << "\n";
    return 0;
}

} // namespace txn

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
            "  --nano       Symbol-only compression (file:kind:code:line) — 95%+ on builds\n"
            "  --gist       One-line semantic TL;DR of the output (test/diff/log/build)\n"
            "               (ICMG_AUTO_ROUTE=1 auto-applies --nano/--gist to learned-noisy cmds)\n"
            "  --txn        Transactional (lite): snapshot tracked files before the\n"
            "               command, show the change set after; then --txn-commit to\n"
            "               accept or --txn-abort to revert (--txn-diff to re-review)\n"
            "  --timeout N  Execution timeout in ms (default: 60000)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        // v2.26.0 transactional run (lite): control verbs + wrap flag.
        if (args[0] == "--txn-diff")   return txn::showDiff();
        if (args[0] == "--txn-commit") return txn::commit();
        if (args[0] == "--txn-abort")  return txn::abortTxn();
        bool txn_wrap = false;
        std::vector<std::string> eff_args = args;
        if (args[0] == "--txn") {
            txn_wrap = true;
            eff_args.erase(eff_args.begin());
            if (eff_args.empty()) { std::cerr << "icmg run --txn: requires <command>\n"; return 1; }
            if (txn::begin() != 0) return 1;
        }

        RunArgs ra = parseRunArgs(eff_args);   // v1.70.0 #178: leading-flag-only + "--" passthrough
        bool raw = ra.raw, json_out = ra.json_out, dry_run = ra.dry_run,
             stream = ra.stream, yes = ra.yes, ultra = ra.ultra,
             no_delta = ra.no_delta, last_full = ra.last_full, no_tier = ra.no_tier;
        bool nano = ra.nano, gist = ra.gist;
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
        // v2.20.0 root-cause #2: also honor a leading `ICMG_ASSUME_YES=1` /
        // `FORCE=1` env-prefix on the wrapped command line -- when a command is
        // auto-wrapped as `icmg run "<cmd>"`, that env was set for the child, so
        // it never reached this process. Treat the inline prefix as the same
        // explicit opt-in the user intended.
        if (hasInlineYesPrefix(cmd_args)) assume_yes = true;
#if defined(_WIN32)
        bool stdin_tty = _isatty(_fileno(stdin)) != 0;
#else
        bool stdin_tty = isatty(fileno(stdin)) != 0;
#endif
        bool is_dest = isDestructiveArgv(cmd_args, dest_reason);
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

        // Pre-exec densify: rewrite noisy commands to emit less (git status ->
        // --porcelain, pytest -> -q --tb=line, ...) BEFORE Tkil filters output.
        // Pure + guarded (skips shell composition); opt out with ICMG_NO_DENSIFY=1.
        if (!std::getenv("ICMG_NO_DENSIFY")) {
            std::string densed = core::densifyCommand(command);
            if (densed != command) {
                std::cerr << "[icmg run] densified: " << command << " -> " << densed << "\n";
                command = densed;
            }
        }

        int rc = executor.runFiltered(command, raw, json_out, dry_run, stream, ultra,
                                      no_delta, last_full, no_tier, nano, gist);
        if (txn_wrap) txn::showDiff();   // v2.26.0: surface the change set
        return rc;
    }
};

ICMG_REGISTER_COMMAND("run", RunCommand);

} // namespace icmg::cli
