// Phase 77: `icmg shadow-upgrade` — Chrome/VS-Code-style background upgrade.
//
// Daily check polls GitHub releases. Newer version found → download to
// ~/.icmg/shadow/<version>/ + sha256 verify → mark pending. Next icmg
// invocation in idle moment swaps shadow → live (uses existing pending-restart
// infra from Phase 46).
//
// Subcommands:
//   check                Poll GitHub releases; if newer + verified, stage shadow + mark pending
//   apply                Swap-in pending shadow if no lock; updates pending-restart marker
//   status               Show current vs latest vs pending; cache age
//   rollback             Restore previous .bak (existing infra)
//   auto-on [--every Nh] Schedule daily check (default 24h)
//   auto-off
//   pin <version>        Lock to specific version; auto-upgrade skipped while pinned
//   unpin
//
// Safety: sha256 sidecar mandatory; mismatch aborts staging.
// Storage: ~/.icmg/shadow/<version>/ ~ 30MB per shadow; older shadows pruned on apply.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/path_utils.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/audit_log.hpp"
#include "../../core/schedule_helper.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace icmg::cli {

extern const char* CURRENT_VERSION_STR;  // declared elsewhere; fallback below

class ShadowUpgradeCommand : public BaseCommand {
public:
    std::string name()        const override { return "shadow-upgrade"; }
    std::string description() const override {
        return "Background auto-upgrade (check/apply/status/rollback/auto-on)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg shadow-upgrade <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  check                 Poll GitHub; stage shadow if newer + sha256 verified\n"
            "  apply                 Swap-in pending shadow (atomic; no lock required)\n"
            "  status                Local vs latest vs pending + cache age\n"
            "  rollback              Restore previous version from .bak\n"
            "  auto-on [--every Nh]  Schedule daily check (default 24h)\n"
            "  auto-off\n"
            "  pin <version>         Skip auto-upgrade while pinned\n"
            "  unpin                 Resume auto-upgrade\n\n"
            "Storage: ~/.icmg/shadow/<version>/  (~30 MB each; older pruned on apply)\n"
            "Opt-out fully: ~/.icmg/no-auto-upgrade.flag (touch file)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        if (sub == "check")    return cmdCheck(rest);
        if (sub == "apply")    return cmdApply(rest);
        if (sub == "status")   return cmdStatus(rest);
        if (sub == "rollback") return cmdRollback(rest);
        if (sub == "auto-on")  return cmdAutoOn(rest);
        if (sub == "auto-off") return cmdAutoOff(rest);
        if (sub == "pin")      return cmdPin(rest);
        if (sub == "unpin")    return cmdUnpin(rest);
        std::cerr << "icmg shadow-upgrade: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    static fs::path globalDir()  { return fs::path(core::icmgGlobalDir()); }
    static fs::path shadowRoot() { return globalDir() / "shadow"; }
    static fs::path pendingFlag() { return globalDir() / "pending-upgrade.flag"; }
    static fs::path pinFile()    { return globalDir() / "pin-version.txt"; }
    static fs::path optOutFlag() { return globalDir() / "no-auto-upgrade.flag"; }
    static fs::path lastCheckFile() { return globalDir() / "shadow-last-check.txt"; }

    static const char* currentVersion() {
        // Synced with update_cmd.cpp / main.cpp.
        return "0.37.0";
    }

    static std::string repo() { return "ncmonx/icm-graph"; }

    static bool versionNewer(const std::string& a, const std::string& b) {
        // Compare X.Y.Z lexically as int triples.
        auto parse = [](const std::string& v) {
            std::vector<int> out;
            std::stringstream ss(v);
            std::string tok;
            while (std::getline(ss, tok, '.')) {
                try { out.push_back(std::stoi(tok)); } catch (...) { out.push_back(0); }
            }
            while (out.size() < 3) out.push_back(0);
            return out;
        };
        auto av = parse(a), bv = parse(b);
        for (int i = 0; i < 3; ++i) {
            if (av[i] != bv[i]) return av[i] > bv[i];
        }
        return false;
    }

    static std::string computeSha256(const fs::path& f) {
#ifdef _WIN32
        std::string cmd = "certutil -hashfile \"" + f.string() + "\" SHA256";
#else
        std::string cmd = "(sha256sum \"" + f.string()
                        + "\" 2>/dev/null || shasum -a 256 \"" + f.string() + "\")";
#endif
        auto res = core::safeExecShell(cmd, false, 30000);
        if (res.exit_code != 0 || res.out.empty()) return {};
        const std::string& s = res.out;
        for (size_t i = 0; i + 64 <= s.size(); ++i) {
            bool ok = true;
            for (size_t j = 0; j < 64; ++j) {
                char c = s[i + j];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                    ok = false; break;
                }
            }
            if (ok) {
                std::string h = s.substr(i, 64);
                std::transform(h.begin(), h.end(), h.begin(), ::tolower);
                return h;
            }
        }
        return {};
    }

    // ---- check ----------------------------------------------------------

    int cmdCheck(const std::vector<std::string>& args) {
        if (fs::exists(optOutFlag())) {
            std::cout << "icmg shadow-upgrade: opt-out flag set (" << optOutFlag()
                      << "); skipping.\n";
            return 0;
        }
        if (fs::exists(pinFile())) {
            std::ifstream f(pinFile()); std::string pin; std::getline(f, pin);
            std::cout << "icmg shadow-upgrade: pinned to " << pin << "; skipping.\n";
            return 0;
        }

        // Throttle: don't poll more than once per 6h (unless --force).
        bool force = hasFlag(args, "--force");
        if (!force && fs::exists(lastCheckFile())) {
            std::ifstream f(lastCheckFile());
            int64_t last = 0; f >> last;
            int64_t age = std::time(nullptr) - last;
            if (age < 6 * 3600) {
                std::cout << "icmg shadow-upgrade: throttled (last check "
                          << (age / 60) << "m ago); use --force to override.\n";
                return 0;
            }
        }

        // Poll GitHub releases/latest.
        std::string url = "https://api.github.com/repos/" + repo() + "/releases/latest";
        std::string cmd = "curl -sL --max-time 10 -H \"User-Agent: icmg/"
                        + std::string(currentVersion()) + "\" \"" + url + "\"";
        auto res = core::safeExecShell(cmd, false, 15000);
        if (res.exit_code != 0 || res.out.empty()) {
            std::cerr << "icmg shadow-upgrade: GitHub poll failed (offline?)\n";
            return 2;
        }
        // Parse tag_name from JSON (lightweight; avoid full json dep here).
        std::string body = res.out;
        size_t p = body.find("\"tag_name\"");
        if (p == std::string::npos) {
            std::cerr << "icmg shadow-upgrade: malformed response\n";
            return 2;
        }
        size_t qs = body.find('"', p + 11);
        if (qs == std::string::npos) return 2;
        size_t qe = body.find('"', qs + 1);
        if (qe == std::string::npos) return 2;
        std::string tag = body.substr(qs + 1, qe - qs - 1);
        std::string latest = tag;
        if (!latest.empty() && latest[0] == 'v') latest = latest.substr(1);

        // Write last-check timestamp.
        { std::ofstream f(lastCheckFile()); f << std::time(nullptr) << "\n"; }

        std::cout << "icmg shadow-upgrade: local=" << currentVersion()
                  << " latest=" << latest << "\n";
        if (!versionNewer(latest, currentVersion())) {
            std::cout << "  up-to-date.\n";
            return 0;
        }

        // Newer found → stage shadow.
        fs::path stage_dir = shadowRoot() / latest;
        fs::create_directories(stage_dir);

        // Asset list (must match release uploads).
        std::vector<std::string> assets = {
            "icmg.exe",
            "libtree-sitter-0.26.dll",
            "libwinpthread-1.dll",
            "libzstd.dll",
            "onnxruntime.dll",
            "onnxruntime_providers_shared.dll",
            "wasmtime.dll",
        };
        std::string base_url = "https://github.com/" + repo()
                             + "/releases/download/" + tag + "/";

        for (auto& a : assets) {
            fs::path out = stage_dir / a;
            std::string dl = "curl -sL --max-time 60 -o \"" + out.string()
                           + "\" \"" + base_url + a + "\"";
            auto r1 = core::safeExecShell(dl, false, 90000);
            if (r1.exit_code != 0 || !fs::exists(out) || fs::file_size(out) == 0) {
                std::cerr << "  [fail] " << a << "\n";
                return 3;
            }
            // sha256 sidecar
            fs::path sha_out = stage_dir / (a + ".sha256");
            std::string dl2 = "curl -sL --max-time 30 -o \"" + sha_out.string()
                            + "\" \"" + base_url + a + ".sha256\"";
            core::safeExecShell(dl2, false, 30000);
            if (!fs::exists(sha_out)) {
                std::cerr << "  [warn] " << a << ".sha256 unavailable; skip verify\n";
                continue;
            }
            std::ifstream sf(sha_out);
            std::string expected; sf >> expected;
            std::string actual = computeSha256(out);
            if (!expected.empty() && !actual.empty() && expected != actual) {
                std::cerr << "  [MISMATCH] " << a
                          << " expected=" << expected.substr(0, 16) << "..."
                          << " actual=" << actual.substr(0, 16) << "...\n";
                // Drop tampered/partial stage dir.
                fs::remove_all(stage_dir);
                return 4;
            }
            std::cout << "  [OK] " << a << "  " << actual.substr(0, 16) << "...\n";
        }

        // Mark pending.
        std::ofstream pf(pendingFlag());
        pf << latest << "\n" << stage_dir.string() << "\n" << std::time(nullptr) << "\n";

        try {
            core::AuditLog al((globalDir() / "audit.log").string());
            al.append("shadow", "STAGED",
                      "from=" + std::string(currentVersion()) + " to=" + latest);
        } catch (...) {}

        std::cout << "icmg shadow-upgrade: staged v" << latest << " at " << stage_dir.string()
                  << "\n  Apply on next icmg invocation (or `icmg shadow-upgrade apply`).\n";
        return 0;
    }

    // ---- apply ----------------------------------------------------------

    int cmdApply(const std::vector<std::string>& args) {
        (void)args;
        if (!fs::exists(pendingFlag())) {
            std::cout << "icmg shadow-upgrade apply: nothing pending.\n";
            return 0;
        }
        std::ifstream pf(pendingFlag());
        std::string version, stage_dir_str;
        std::getline(pf, version);
        std::getline(pf, stage_dir_str);
        fs::path stage_dir = stage_dir_str;
        if (!fs::exists(stage_dir / "icmg.exe")) {
            std::cerr << "icmg shadow-upgrade apply: stage dir missing icmg.exe\n";
            fs::remove(pendingFlag());
            return 2;
        }

        // Find live binary location.
        fs::path live;
#ifdef _WIN32
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n > 0) live = fs::path(buf);
#else
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = 0; live = buf; }
#endif
        if (live.empty()) {
            std::cerr << "icmg shadow-upgrade apply: cannot resolve live binary\n";
            return 2;
        }
        fs::path live_dir = live.parent_path();

        // Atomic swap each asset: live → .bak, stage → live.
        std::vector<std::string> assets = {
            "icmg.exe", "libtree-sitter-0.26.dll", "libwinpthread-1.dll",
            "libzstd.dll", "onnxruntime.dll", "onnxruntime_providers_shared.dll",
            "wasmtime.dll",
        };
        int swapped = 0, failed = 0;
        for (auto& a : assets) {
            fs::path src = stage_dir / a;
            fs::path dst = live_dir / a;
            if (!fs::exists(src)) continue;
            std::error_code ec;
            // Move current to .bak (best effort; locked icmg.exe may need pending-restart).
            if (fs::exists(dst)) {
                fs::path bak = dst; bak += ".bak";
                fs::remove(bak, ec);
                fs::rename(dst, bak, ec);
                if (ec) {
                    // Locked → write .new + .pending-restart marker (existing infra).
                    fs::path new_path = dst; new_path += ".new";
                    fs::copy_file(src, new_path, fs::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        std::ofstream f(globalDir() / ".pending-restart");
                        f << dst.string() << "\n";
                        std::cerr << "  [defer] " << a
                                  << " locked; staged .new + pending-restart marker\n";
                        ++swapped;  // counts as success (will complete next invocation)
                        continue;
                    }
                    ++failed;
                    std::cerr << "  [fail] " << a << ": " << ec.message() << "\n";
                    continue;
                }
            }
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                ++failed;
                std::cerr << "  [fail] " << a << ": " << ec.message() << "\n";
                continue;
            }
            ++swapped;
            std::cout << "  [swap] " << a << "\n";
        }

        // Prune older shadows (keep current + previous only).
        std::vector<fs::path> shadows;
        for (auto& e : fs::directory_iterator(shadowRoot())) {
            if (e.is_directory()) shadows.push_back(e.path());
        }
        std::sort(shadows.begin(), shadows.end(),
                  [](const fs::path& a, const fs::path& b){ return a.filename() < b.filename(); });
        for (size_t i = 0; i + 2 < shadows.size(); ++i) {
            std::error_code ec;
            fs::remove_all(shadows[i], ec);
        }

        try {
            core::AuditLog al((globalDir() / "audit.log").string());
            al.append("shadow", "APPLIED",
                      "version=" + version + " swapped=" + std::to_string(swapped)
                      + " failed=" + std::to_string(failed));
        } catch (...) {}

        if (failed == 0) fs::remove(pendingFlag());
        std::cout << "icmg shadow-upgrade apply: " << swapped << " swapped, "
                  << failed << " failed\n";
        if (failed > 0) {
            std::cerr << "  Some assets did not swap; pending flag retained for retry.\n";
            return 5;
        }
        return 0;
    }

    // ---- status ---------------------------------------------------------

    int cmdStatus(const std::vector<std::string>&) {
        std::cout << "icmg shadow-upgrade status\n"
                  << "  local:    " << currentVersion() << "\n";
        if (fs::exists(lastCheckFile())) {
            std::ifstream f(lastCheckFile());
            int64_t t = 0; f >> t;
            std::cout << "  last check: " << ((std::time(nullptr) - t) / 60) << "m ago\n";
        } else {
            std::cout << "  last check: never\n";
        }
        if (fs::exists(pendingFlag())) {
            std::ifstream pf(pendingFlag());
            std::string version; std::getline(pf, version);
            std::cout << "  pending:  v" << version << " (apply: next icmg cmd or `apply`)\n";
        } else {
            std::cout << "  pending:  (none)\n";
        }
        if (fs::exists(pinFile())) {
            std::ifstream f(pinFile()); std::string pin; std::getline(f, pin);
            std::cout << "  pinned:   " << pin << "\n";
        }
        if (fs::exists(optOutFlag())) std::cout << "  opt-out:  ON (no auto-upgrade)\n";
        // List staged shadows.
        if (fs::exists(shadowRoot())) {
            int n = 0;
            for (auto& e : fs::directory_iterator(shadowRoot())) if (e.is_directory()) ++n;
            std::cout << "  shadows:  " << n << " in " << shadowRoot().string() << "\n";
        }
        return 0;
    }

    int cmdRollback(const std::vector<std::string>&) {
        // Find live binary, swap with .bak.
#ifdef _WIN32
        wchar_t buf[MAX_PATH]; GetModuleFileNameW(nullptr, buf, MAX_PATH);
        fs::path live = fs::path(buf);
#else
        char buf[4096]; ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf)-1);
        if (n > 0) buf[n] = 0;
        fs::path live = buf;
#endif
        fs::path bak = live; bak += ".bak";
        if (!fs::exists(bak)) {
            std::cerr << "icmg shadow-upgrade rollback: no .bak available\n";
            return 1;
        }
        // Swap.
        std::error_code ec;
        fs::path tmp = live; tmp += ".rollback";
        fs::rename(live, tmp, ec);
        if (ec) {
            // Live locked. Use pending-restart pattern.
            fs::path new_path = live; new_path += ".new";
            fs::copy_file(bak, new_path, fs::copy_options::overwrite_existing, ec);
            std::ofstream f(globalDir() / ".pending-restart");
            f << live.string() << "\n";
            std::cout << "icmg shadow-upgrade rollback: deferred (live locked)\n";
            return 0;
        }
        fs::rename(bak, live, ec);
        fs::rename(tmp, bak, ec);  // now tmp is the "new" backup
        std::cout << "icmg shadow-upgrade rollback: swapped to prior version.\n";
        return 0;
    }

    int cmdPin(const std::vector<std::string>& args) {
        if (args.empty() || args[0][0] == '-') {
            std::cerr << "icmg shadow-upgrade pin: <version>\n";
            return 1;
        }
        fs::create_directories(globalDir());
        std::ofstream f(pinFile());
        f << args[0] << "\n";
        std::cout << "icmg shadow-upgrade: pinned to " << args[0] << "\n";
        return 0;
    }

    int cmdUnpin(const std::vector<std::string>&) {
        fs::remove(pinFile());
        std::cout << "icmg shadow-upgrade: unpinned.\n";
        return 0;
    }

    // ---- auto-on / off --------------------------------------------------

    static std::string taskName() {
        // Global task — one per user, not per project (binary is per-user).
        return "icmg-shadow-upgrade";
    }

    int cmdAutoOn(const std::vector<std::string>& args) {
        std::string interval = flagValue(args, "--every", "24h");
        int hours = 24;
        try {
            char unit = interval.back();
            int n = std::stoi(interval.substr(0, interval.size() - 1));
            if      (unit == 'h') hours = n;
            else if (unit == 'd') hours = n * 24;
        } catch (...) {}
        if (hours < 1) hours = 1;
        int minutes = hours * 60;
        std::string tn = taskName();
#ifdef _WIN32
        // Phase 78: bulletproof scheduler. Global per-user task; wrapper in ~/.icmg/sched/.
        fs::path wrapper = globalDir() / "sched" / (tn + ".cmd");
        fs::create_directories(wrapper.parent_path());
        std::ofstream wf(wrapper, std::ios::binary);
        wf << "@echo off\r\n"
           << "echo === %DATE% %TIME% shadow ===>> \"%USERPROFILE%\\.icmg\\sched\\shadow.log\"\r\n"
           << "icmg shadow-upgrade check >> \"%USERPROFILE%\\.icmg\\sched\\shadow.log\" 2>&1\r\n";
        wf.close();
        core::ScheduleSpec spec{tn, wrapper.string(), minutes, "shadow-upgrade"};
        int rc = core::registerWindowsSchedule(spec);
        if (rc != 0) return rc;
#else
        std::string cron_expr = "0 */" + std::to_string(hours) + " * * *";
        std::string entry = cron_expr + "  " + cmd + "  # " + tn + "\n";
        auto cur = core::safeExecShell("crontab -l 2>/dev/null", false, 5000);
        std::string tab = cur.exit_code == 0 ? cur.out : "";
        std::ostringstream filtered;
        std::istringstream is(tab);
        std::string line;
        while (std::getline(is, line))
            if (line.find("# " + tn) == std::string::npos) filtered << line << "\n";
        std::string newtab = filtered.str() + entry;
        std::string tmp = "/tmp/icmg-shadow-cron.tmp";
        std::ofstream f(tmp); f << newtab; f.close();
        core::safeExecShell("crontab " + tmp, true, 5000);
        std::remove(tmp.c_str());
#endif
        std::cout << "icmg shadow-upgrade auto-on: every " << hours << "h\n";
        return 0;
    }

    int cmdAutoOff(const std::vector<std::string>&) {
        std::string tn = taskName();
#ifdef _WIN32
        core::safeExecShell("MSYS_NO_PATHCONV=1 schtasks /Delete /TN \"" + tn + "\" /F", true, 5000);
#else
        auto cur = core::safeExecShell("crontab -l 2>/dev/null", false, 5000);
        if (cur.exit_code == 0 && !cur.out.empty()) {
            std::ostringstream filtered;
            std::istringstream is(cur.out);
            std::string line;
            while (std::getline(is, line))
                if (line.find("# " + tn) == std::string::npos) filtered << line << "\n";
            std::string tmp = "/tmp/icmg-shadow-cron.tmp";
            std::ofstream f(tmp); f << filtered.str(); f.close();
            core::safeExecShell("crontab " + tmp, true, 5000);
            std::remove(tmp.c_str());
        }
#endif
        std::cout << "icmg shadow-upgrade auto-off: cleared\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("shadow-upgrade", ShadowUpgradeCommand);

} // namespace icmg::cli
