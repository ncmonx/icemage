#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "core/config.hpp"
#include "core/version.hpp"
#include "core/db.hpp"
#include "core/migrator.hpp"
#include "core/logger.hpp"
#include "core/exec_utils.hpp"
#include "core/path_utils.hpp"
#include "core/version_check.hpp"
#include "core/crash_hint.hpp"
#include "core/dll_trace.hpp"
#include "core/openssl_rng.hpp"
#include "cli/dispatcher.hpp"
#include "mcp/server.hpp"
#include <system_error>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
// v1.5.2: sweep leftover *.dll.old-<pid> sidecars from rename-aside upgrades.
// These are created by `icmg update --apply` when a DLL was locked at copy
// time. We remove them at startup; ignore any still-locked ones (some other
// icmg instance may still hold a handle).
static void sweepDllOldSidecars() {
    char buf[1024]; DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n == 0) return;
    fs::path bin = buf;
    fs::path dir = bin.parent_path();
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) { ec.clear(); continue; }
        const std::string& name = e.path().filename().string();
        if (name.find(".dll.old-") == std::string::npos) continue;
        std::error_code rm;
        fs::remove(e.path(), rm);  // best-effort; locked sidecars stay
    }
}

// v0.58.1 popup fix: icmg.exe links as /SUBSYSTEM:WINDOWS (no auto-allocated
// console). When invoked from an existing console (cmd/bash/powershell), we
// attach to the parent's console so stdout/stderr/stdin work normally for
// interactive use. When invoked headless (Task Scheduler / Explorer / parent
// without console), AttachConsole returns false → nothing flashes.
static void attachParentConsoleIfAny() {
    // Snapshot whether parent passed real stdio handles (bash/mintty pipes,
    // shell redirects, etc.) BEFORE we touch the console.
    HANDLE h_out_pre = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE h_err_pre = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE h_in_pre  = GetStdHandle(STD_INPUT_HANDLE);
    bool inherited_out = (h_out_pre != nullptr && h_out_pre != INVALID_HANDLE_VALUE);
    bool inherited_err = (h_err_pre != nullptr && h_err_pre != INVALID_HANDLE_VALUE);
    bool inherited_in  = (h_in_pre  != nullptr && h_in_pre  != INVALID_HANDLE_VALUE);

    // Try to attach to parent's Windows console. Fails (returns FALSE) when:
    //   - parent has no console (Task Scheduler, Explorer, daemon ancestry) →
    //     icmg runs fully headless, no flash possible.
    //   - parent is bash/mintty/conpty (uses pseudo-tty pipes, not classic
    //     console) → AttachConsole fails too, but stdio pipes are still
    //     inherited so output reaches the tty fine via the pre handles.
    bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;

    // Rebind C stdio to CONOUT$/CONIN$ ONLY for the handles parent did NOT
    // already pass through. Otherwise we'd clobber working pipes (e.g. bash
    // redirecting icmg's stdout to a file).
    FILE* fp = nullptr;
    if (attached && !inherited_out) (void)freopen_s(&fp, "CONOUT$", "w", stdout);
    if (attached && !inherited_err) (void)freopen_s(&fp, "CONOUT$", "w", stderr);
    if (attached && !inherited_in)  (void)freopen_s(&fp, "CONIN$",  "r", stdin);
    std::ios::sync_with_stdio(true);
    std::cout.clear();
    std::cerr.clear();
    std::cin.clear();

    // v1.20.5: when attached to a parent CMD prompt, install an atexit
    // handler that flushes stdio, sends a newline to the parent console
    // input buffer, and frees the console. Without this, CMD's prompt
    // appears BEFORE the child's last output flushes (GUI-subsystem child
    // doesn't block CMD), and the user has to press a key to wake the
    // shell. Idempotent: no-op when not attached.
    if (attached) {
        static const bool _exit_handler_installed = [](){
            std::atexit([](){
                std::fflush(stdout);
                std::fflush(stderr);
                // Inject a CR into parent's input buffer so the CMD shell
                // redraws its prompt cleanly on a fresh line.
                HANDLE h_in = GetStdHandle(STD_INPUT_HANDLE);
                if (h_in && h_in != INVALID_HANDLE_VALUE) {
                    INPUT_RECORD r[2] = {};
                    r[0].EventType = KEY_EVENT;
                    r[0].Event.KeyEvent.bKeyDown = TRUE;
                    r[0].Event.KeyEvent.wRepeatCount = 1;
                    r[0].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
                    r[0].Event.KeyEvent.uChar.AsciiChar = '\r';
                    r[1] = r[0];
                    r[1].Event.KeyEvent.bKeyDown = FALSE;
                    DWORD written = 0;
                    WriteConsoleInputA(h_in, r, 2, &written);
                }
                FreeConsole();
            });
            return true;
        }();
        (void)_exit_handler_installed;
    }

    // v1.47.0: UTF-8 console output so emoji/multibyte glyphs
    // (Indonesian gaul, Chinese, Japanese, etc.) render correctly
    // instead of becoming mojibake (= ≡fÿ etc.) on CP1252 cmd.exe.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}
#endif

// Auto-install hooks when icmg first enters a project dir (.claude/ or .git/
// present but .icmg/ not yet created). Skipped for `init` and `update` to
// avoid recursion / interference with those commands.
static void autoBootstrapProject(const std::vector<std::string>& args) {
    if (!args.empty() && (args[0] == "init" || args[0] == "update")) return;
    auto cwd = fs::current_path();
    if (fs::exists(cwd / ".icmg")) return;
    if (!fs::exists(cwd / ".claude") && !fs::exists(cwd / ".git")) return;
    icmg::core::safeExecShell(
        "icmg init --install-hooks --force --no-agents --no-embedder "
        "--no-scan --no-backup --no-maintain --no-mirror "
        "--no-sentinel --no-auto-upgrade",
        false, 25000);
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // v1.3.1: suppress Windows critical-error popups (e.g. "B:/ — system
    // cannot find drive"). MSYS-style bash paths like /b/x can reach Win32
    // file APIs as `B:\x`. SEM_FAILCRITICALERRORS makes the call fail
    // silently instead of showing a system dialog; SEM_NOOPENFILEERRORBOX
    // suppresses the "file not found" UI.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    // B:/ "insert disk" popup ROOT FIX (2026-06-12): under git-bash/MSYS, the
    // path-converter rewrites cmd.exe flags that look like POSIX paths into drive
    // paths (/c -> C:\, and crucially /B -> B:\). When icmg spawns a subprocess
    // wrapped in cmd.exe, an unconvertible drive (B:) makes cmd.exe raise the
    // modal "B:/ cannot find drive" dialog on EVERY spawn (regression since the
    // subprocess-spawning grew ~v0.50). Disabling MSYS path/arg conversion in
    // icmg's environment propagates to every child shell, so flags pass verbatim.
    SetEnvironmentVariableA("MSYS_NO_PATHCONV", "1");
    SetEnvironmentVariableA("MSYS2_ARG_CONV_EXCL", "*");

    // Loader tracer: remember the last DLL loaded (+ ICMG_TRACE_DLL=1 streams all)
    // so an err126 crash can name the subsystem that was initializing. Catches
    // runtime LoadLibrary-by-name modules invisible to the PE import walk.
    icmg::core::installDllTracer();
    // err126 root fix (Server 2019 / Core): route OpenSSL's RNG onto BCrypt so
    // SQLCipher writes never load the missing CryptoAPI (rsaenh) module. Must run
    // before any encrypted DB is opened. Opt out with ICMG_NO_RAND_OVERRIDE=1.
    if (!std::getenv("ICMG_NO_RAND_OVERRIDE")) icmg::core::installBCryptOpenSSLRand();

    // v1.19.0: sanitize PATH inside icmg-core too — when icmg-core is invoked
    // directly (Task Scheduler, Startup folder, schtask, user manual exec),
    // exec_client's sanitize_path() never ran. Strip PATH entries pointing
    // to non-existent drives (B:\ from MSYS /b/ paths is the chronic culprit).
    // This must run BEFORE any DLL LoadLibrary or fs::exists() probe so the
    // Win loader's PATH scan for delay-loaded DLLs sees the clean PATH.
    {
        char* path = std::getenv("PATH");
        if (path) {
            DWORD drives = GetLogicalDrives();
            size_t len = std::strlen(path);
            std::vector<char> out(len + 1, '\0');
            size_t oi = 0;
            const char* start = path;
            for (const char* p = path; ; ++p) {
                if (*p == ';' || *p == '\0') {
                    size_t seg_len = (size_t)(p - start);
                    bool skip = false;
                    if (seg_len >= 2 && start[1] == ':') {
                        char drv = start[0];
                        if (drv >= 'a' && drv <= 'z') drv = (char)(drv - 32);
                        if (drv >= 'A' && drv <= 'Z') {
                            int bit = drv - 'A';
                            if (!((drives >> bit) & 1)) skip = true;
                        }
                    }
                    if (!skip && seg_len > 0) {
                        if (oi > 0) out[oi++] = ';';
                        std::memcpy(out.data() + oi, start, seg_len);
                        oi += seg_len;
                    }
                    if (*p == '\0') break;
                    start = p + 1;
                }
            }
            SetEnvironmentVariableA("PATH", out.data());
        }
    }

    attachParentConsoleIfAny();
    sweepDllOldSidecars();
#endif
    // Parse global flags first
    std::vector<std::string> args;
    bool verbose = false;
    bool show_version = false;
    bool mcp_server = false;

    // Stop global-flag parsing once we hit the first subcommand. Otherwise
    // commands like `icmg run node --version` would eat --version globally
    // and never reach the subprocess.
    bool seen_subcommand = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (!seen_subcommand) {
            if (a == "--verbose" || a == "-v") { verbose = true; continue; }
            if (a == "--version")              { show_version = true; continue; }
            if (a == "--mcp-server")           { mcp_server = true; continue; }
            if (!a.empty() && a[0] != '-')     seen_subcommand = true;
        }
        args.push_back(a);
    }

    if (show_version) {
        std::cout << "icmg " << icmg::core::ICMG_VERSION << "\n";
        return 0;
    }

    // Init config
    auto& cfg = icmg::core::Config::instance();
    cfg.load();
    if (verbose) cfg.setVerbose(true);

    // Init logger
    icmg::core::Logger::instance().init(cfg.logPath());

    // v1.6.2: skip eager DB init for hot-path commands that don't need DB.
    // Reduces first-prompt latency on cold cache; hook handlers, shield, and
    // popup-killer were paying ~50-200ms DB open + migration check on every
    // invocation. Defer DB open to the cmd handler itself (lazy).
    auto is_hot_path = [&]() {
        if (args.empty()) return false;
        const std::string& cmd = args[0];
        // v1.6.7: extended hot-path list. These cmds do NOT touch project
        // DB. Skipping ensureProjectDb avoids WAL-lock contention when many
        // icmg instances are concurrently active (cron tick, hook fires).
        return cmd == "hook" || cmd == "shield" || cmd == "popup-killer"
            || cmd == "--help" || cmd == "-h"
            || cmd == "completions" || cmd == "version"
            || cmd == "update" || cmd == "daemon" || cmd == "service"
            || cmd == "cronjobs" || cmd == "shadow-upgrade" || cmd == "cleanup";
    };

    std::string db_path;
    if (!is_hot_path()) {
        try {
            db_path = cfg.projectDbPath(".");
            icmg::core::ensureProjectDb(db_path);
        } catch (const std::exception& e) {
            // v1.56 HOTFIX (was v1.47.0): orphan-DB fallback was a misdiagnosis
            // magnet. Old logic: ANY ensureProjectDb failure → orphan, with
            // the error hidden as "cwd unwritable". Real causes encountered:
            //   - locked DB (another icmg running on same project)
            //   - sqlite UTF-8 path encoding bug on Unicode CWDs
            //   - schema/migration error (older binary, newer DB)
            //   - antivirus quarantine on data.db
            // Result: user lands on an EMPTY orphan and project memory looks
            // wiped. v1.56 hardens with 3 guards:
            //   (1) ALWAYS print the original e.what() — no silent masking.
            //   (2) If project DB already exists in cwd → refuse orphan;
            //       force the real error to surface.
            //   (3) Probe-test cwd writability with a throwaway file. Only
            //       trigger orphan when the probe ALSO fails.
            std::cerr << "icmg: db init error: " << e.what()
                      << "\n  -> path: " << db_path << "\n";

            // (2) Project DB exists → orphan would replace real data. Bail.
            bool project_db_exists = false;
            try { project_db_exists = fs::exists(fs::path(db_path)); } catch (...) {}
            if (project_db_exists) {
                std::cerr << "icmg: project DB already exists in cwd — "
                             "refusing to fall back to orphan DB. Fix the "
                             "error above (other icmg process? migration? "
                             "file lock? AV?) then retry.\n";
                db_path.clear();   // non-DB commands still work
            } else {
                // (3) Probe cwd writability with a real touch.
                bool cwd_writable = false;
                try {
                    fs::path probe = fs::path(".") / ".icmg-probe.tmp";
                    { std::ofstream of(probe.string()); of << "x"; }
                    if (fs::exists(probe)) {
                        cwd_writable = true;
                        std::error_code ec;
                        fs::remove(probe, ec);
                    }
                } catch (...) {}

                if (cwd_writable) {
                    std::cerr << "icmg: cwd IS writable — refusing to fall "
                                 "back to orphan DB. The error above is NOT "
                                 "a permission issue (likely sqlite encoding, "
                                 "schema, or lock). Fix and retry.\n";
                    db_path.clear();
                } else {
                    bool orphan_ok = false;
                    std::string exe = icmg::core::selfExePath();
                    if (!exe.empty()) {
                        fs::path exe_dir = fs::path(exe).parent_path();
                        fs::path orphan = exe_dir / "icmg-orphan.db";
                        try {
                            icmg::core::ensureProjectDb(orphan.string());
                            db_path = orphan.string();
                            cfg.setProjectDbOverride(db_path);
                            std::cerr << "icmg: cwd unwritable (probe failed); "
                                         "using orphan DB at " << db_path << "\n";
                            orphan_ok = true;
                        } catch (...) {}
                    }
                    if (!orphan_ok) {
                        std::cerr << "icmg: orphan fallback also failed "
                                     "(non-DB commands still work)\n";
                    }
                }
            }
        }
    } else {
        // Resolve path lazily for cmds that may need it; do not open/migrate.
        try { db_path = cfg.projectDbPath("."); } catch (...) {}
    }

    // MCP server mode — run stdio JSON-RPC loop
    if (mcp_server) {
        try {
            icmg::core::Db db(db_path);
            icmg::mcp::McpServer server(db);
            server.run();
        } catch (const std::exception& e) {
            std::cerr << "icmg: mcp server error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    // Auto-bootstrap project hooks on first entry (silent, non-blocking).
    autoBootstrapProject(args);

    // v1.40.2: stale-version check DISABLED — fast iteration cycle means
    // releases land every few hours; nag/soft-block was misfiring on
    // perfectly current installs. Re-enable when v1.41.x release cadence
    // slows. Opt-in via ICMG_VERSION_CHECK=1 to restore old behavior.
    if (std::getenv("ICMG_VERSION_CHECK")
        && !args.empty() && args[0] != "update" && args[0] != "upgrade") {
        auto vstatus = icmg::core::checkVersionStaleness(icmg::core::ICMG_VERSION);
        icmg::core::printVersionWarning(vstatus);
        if (!args.empty() && icmg::core::isCommandSoftBlocked(args[0], vstatus)) {
            std::cerr << "[icmg] Command '" << args[0]
                      << "' is disabled on this stale version. Run: icmg upgrade\n";
            return 1;
        }
    }

    // Dispatch command + catch-all that auto-captures crashes for
    // `icmg bug-report --send-pending`. Privacy: never auto-submits; only
    // writes to ~/.icmg/crash-pending.jsonl until user opts in.
    icmg::cli::Dispatcher dispatcher;
    try {
        return dispatcher.run(args);
    } catch (const std::exception& e) {
        // Synthesize a one-shot --auto-capture invocation so the crash gets
        // recorded with the same code path users would invoke manually.
        std::string cmd_str;
        for (auto& a : args) { if (!cmd_str.empty()) cmd_str += " "; cmd_str += a; }
        std::vector<std::string> cap = {
            "bug-report", "--auto-capture",
            "--cmd", cmd_str,
            "--err", e.what()
        };
        try {
            icmg::cli::Dispatcher d2;
            d2.run(cap);
        } catch (...) { /* swallow — never crash twice */ }
        int sys_code = 0;
        if (auto fse = dynamic_cast<const std::filesystem::filesystem_error*>(&e)) {
            sys_code = fse->code().value();
            std::cerr << "icmg: filesystem error code " << sys_code
                      << " (" << fse->code().message() << ")\n";
            if (!fse->path1().empty()) std::cerr << "      path1: " << fse->path1().string() << "\n";
            if (!fse->path2().empty()) std::cerr << "      path2: " << fse->path2().string() << "\n";
        } else if (auto se = dynamic_cast<const std::system_error*>(&e)) {
            sys_code = se->code().value();
            std::cerr << "icmg: system error code " << sys_code
                      << " (" << se->code().message() << ")\n";
        }
        std::cerr << "icmg: uncaught error: " << e.what() << "\n"
                  << "      Crash logged. Send report: icmg bug-report --send-pending\n";
        // err126 (module-load) self-diagnosis: tell the user how to capture the
        // exact missing module -- the name lives in the OS loader, not e.what().
        std::string mhint = icmg::core::moduleLoadHint(e.what(), sys_code);
        if (!mhint.empty()) {
            const std::string& last = icmg::core::lastLoadedDll();
            if (!last.empty())
                std::cerr << "      last DLL loaded before crash: " << last
                          << "  (the missing module is one this loads at runtime;\n"
                          << "       re-run with ICMG_TRACE_DLL=1 to see the full load order)\n";
            std::cerr << mhint;
            // DEGRADE: if a read command (context) crashed on a host module-load
            // err126 (e.g. SQLCipher write-side crypto on Windows Server), emit the
            // raw file to stdout so the caller still gets content instead of losing
            // read access. Find the first non-flag arg as the file path.
            if (!args.empty() && (args[0] == "context" || args[0] == "context-node")) {
                std::string fileArg;
                for (size_t i = 1; i < args.size(); ++i)
                    if (!args[i].empty() && args[i][0] != '-') { fileArg = args[i]; break; }
                if (!fileArg.empty()) {
                    std::ifstream rf(fileArg, std::ios::binary);
                    if (rf) {
                        std::cerr << "icmg context: degraded to raw file (host err126; "
                                     "graph/bundle unavailable on this machine).\n";
                        std::cout << rf.rdbuf();
                        return 0;
                    }
                }
            }
        }
        return 1;
    } catch (...) {
        std::vector<std::string> cap = {
            "bug-report", "--auto-capture",
            "--cmd", args.empty() ? "" : args[0],
            "--err", "unknown exception"
        };
        try {
            icmg::cli::Dispatcher d2;
            d2.run(cap);
        } catch (...) {}
        std::cerr << "icmg: unknown crash. Send report: icmg bug-report --send-pending\n";
        return 1;
    }
}
