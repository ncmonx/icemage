// Phase 27 T1: `icmg update` — self-upgrade via github releases.
//
// --check : GET /releases/latest → semver compare with current.
// --apply : download asset for platform → atomic swap (Win: rename .bak, write new).
// --rollback : restore .bak.
//
// Network call via system curl (no libcurl dep). 10s timeout. HTTPS-only.
// Hard-coded host github.com. No auto-cron — explicit user invocation only.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>
#include <regex>
#include <algorithm>
#include <cctype>
#ifdef _WIN32
  #include <windows.h>
#else
  #include <sys/wait.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

static const char* CURRENT_VERSION = "0.35.1";   // keep synced with main.cpp / mcp/server.cpp
static const char* REPO            = "ncmonx/icm-graph";

// Returns -1 if a < b, 0 if equal, +1 if a > b. Tolerant to "v" prefix.
static int semverCmp(const std::string& a, const std::string& b) {
    auto strip = [](std::string s) {
        if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
        return s;
    };
    auto parts = [](const std::string& s) {
        std::vector<int> p;
        std::stringstream ss(s);
        std::string seg;
        while (std::getline(ss, seg, '.')) {
            try { p.push_back(std::stoi(seg)); } catch (...) { p.push_back(0); }
        }
        while (p.size() < 3) p.push_back(0);
        return p;
    };
    auto pa = parts(strip(a)), pb = parts(strip(b));
    for (size_t i = 0; i < 3; ++i) {
        if (pa[i] < pb[i]) return -1;
        if (pa[i] > pb[i]) return 1;
    }
    return 0;
}

class UpdateCommand : public BaseCommand {
public:
    std::string name()        const override { return "update"; }
    std::string description() const override { return "Check / apply self-update from github releases"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg update <action> [options]\n\n"
            "Actions:\n"
            "  --check                Compare current to latest release\n"
            "  --apply                Download + atomic swap binary\n"
            "  --rollback             Restore .bak (if present)\n\n"
            "Options:\n"
            "  --channel preview      Use latest pre-release\n"
            "  --skip-verify          Skip SHA256 check (not recommended)\n"
            "  --json\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool check    = hasFlag(args, "--check");
        bool apply    = hasFlag(args, "--apply");
        bool rollback = hasFlag(args, "--rollback");
        bool preview  = flagValue(args, "--channel") == "preview";
        bool json_out = hasFlag(args, "--json");
        bool skip_verify = hasFlag(args, "--skip-verify");
        bool no_auto_rollback = hasFlag(args, "--no-auto-rollback");

        if (rollback) return doRollback();
        if (!check && !apply) { usage(); return 1; }

        auto latest = fetchLatest(preview);
        if (latest.tag.empty()) {
            std::cerr << "icmg update: failed to query github (network or rate-limit)\n";
            return 2;
        }

        int cmp = semverCmp(CURRENT_VERSION, latest.tag);
        if (json_out) {
            std::cout << "{\"current\":\"" << CURRENT_VERSION << "\","
                      << "\"latest\":\"" << latest.tag << "\","
                      << "\"newer_available\":" << (cmp < 0 ? "true" : "false") << "}\n";
        } else {
            std::cout << "Current: " << CURRENT_VERSION << "\n"
                      << "Latest:  " << latest.tag << "\n";
            if (cmp == 0)      std::cout << "Up to date.\n";
            else if (cmp > 0)  std::cout << "Local is newer (dev build).\n";
            else               std::cout << "Update available.\n";
        }
        if (check || cmp >= 0) return 0;
        if (!apply) return 0;

        return doApply(latest, skip_verify, no_auto_rollback);
    }

private:
    struct Release { std::string tag; std::string asset_url; std::string sha256; };

    Release fetchLatest(bool preview) {
        Release r;
        std::string url = preview
            ? std::string("https://api.github.com/repos/") + REPO + "/releases?per_page=1"
            : std::string("https://api.github.com/repos/") + REPO + "/releases/latest";
        std::string cmd = "curl -sL --max-time 10 -H \"User-Agent: icmg/"
                        + std::string(CURRENT_VERSION) + "\" \"" + url + "\"";
        auto res = core::safeExecShell(cmd, false, 12000);
        if (res.exit_code != 0 || res.out.empty()) return r;
        try {
            auto j = json::parse(res.out);
            if (preview && j.is_array()) {
                if (j.empty()) return r;
                j = j[0];
            }
            r.tag = j.value("tag_name", "");
            // Find platform asset.
            std::string want = wantedAssetName();
            if (j.contains("assets") && j["assets"].is_array()) {
                for (auto& a : j["assets"]) {
                    std::string n = a.value("name", "");
                    if (n == want) {
                        r.asset_url = a.value("browser_download_url", "");
                        break;
                    }
                }
            }
        } catch (...) {}
        return r;
    }

    static std::string wantedAssetName() {
#ifdef _WIN32
        return "icmg.exe";
#else
        return "icmg";
#endif
    }

    static fs::path selfPath() {
#ifdef _WIN32
        char buf[1024]; GetModuleFileNameA(nullptr, buf, sizeof(buf));
        return buf;
#else
        return fs::canonical("/proc/self/exe");
#endif
    }

    // Phase 50 T1: download + verify sha256 manifest. Returns true on match
    // or when skip_verify=true. False on hard mismatch (caller aborts).
    // Warns and proceeds when manifest absent (transition period — older
    // releases pre-v0.35.1 lack .sha256 sidecar files).
    bool verifySha256(const fs::path& downloaded, const std::string& asset_url,
                       bool skip_verify) {
        if (skip_verify) {
            std::cerr << "icmg update: --skip-verify passed; skipping integrity check\n";
            return true;
        }
        std::string sha_url = asset_url + ".sha256";
        fs::path sha_tmp = downloaded; sha_tmp += ".sha256";
        std::string sh_path = sha_tmp.string();
        for (auto& c : sh_path) if (c == '\\') c = '/';  // bash-c safe
        std::string cmd = "curl -sL --max-time 10 -o \"" + sh_path
                        + "\" \"" + sha_url + "\"";
        auto res = core::safeExecShell(cmd, false, 12000);
        if (res.exit_code != 0 || !fs::exists(sha_tmp) || fs::file_size(sha_tmp) < 16) {
            std::cerr << "icmg update: WARNING — sha256 manifest unavailable at "
                      << sha_url << "\n"
                      << "  Older release without integrity manifest. Proceeding "
                      << "without verification. Pass --skip-verify to silence.\n";
            fs::remove(sha_tmp);
            return true;  // transition period: don't break existing users
        }
        std::ifstream sf(sha_tmp);
        std::string expected;
        sf >> expected;  // first whitespace-separated token = sha256 hex
        sf.close();
        fs::remove(sha_tmp);
        std::transform(expected.begin(), expected.end(), expected.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (expected.size() != 64) {
            std::cerr << "icmg update: malformed sha256 manifest (got '"
                      << expected << "'); aborting\n";
            return false;
        }

        // Compute sha256 of downloaded file via OS tool.
        std::string actual = computeSha256(downloaded);
        if (actual.empty()) {
            std::cerr << "icmg update: WARNING — cannot compute sha256 (no certutil/"
                      << "sha256sum/shasum on PATH); skipping verification\n";
            return true;
        }
        if (actual != expected) {
            std::cerr << "icmg update: SHA256 MISMATCH — file integrity check FAILED\n"
                      << "  expected: " << expected << "\n"
                      << "  actual:   " << actual << "\n"
                      << "  Aborting. The release artifact may be tampered.\n"
                      << "  Override with --skip-verify (NOT recommended).\n";
            return false;
        }
        std::cout << "icmg update: sha256 verified (" << expected.substr(0, 16) << "...)\n";
        return true;
    }

    static std::string computeSha256(const fs::path& file) {
#ifdef _WIN32
        // certutil -hashfile <path> SHA256 → outputs hash on a line w/o spaces
        std::string cmd = "certutil -hashfile \"" + file.string() + "\" SHA256";
#else
        std::string cmd = "(sha256sum \"" + file.string()
                        + "\" 2>/dev/null || shasum -a 256 \"" + file.string() + "\")";
#endif
        auto res = core::safeExecShell(cmd, false, 30000);
        if (res.exit_code != 0 || res.out.empty()) return {};
        // Parse: find first 64-hex-char token in output.
        std::string out = res.out;
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        std::regex hex_re("[0-9a-f]{64}");
        std::smatch m;
        if (std::regex_search(out, m, hex_re)) return m.str();
        return {};
    }

    int doApply(const Release& r, bool skip_verify, bool no_auto_rollback = false) {
        if (r.asset_url.empty()) {
            std::cerr << "icmg update: no platform asset on release " << r.tag << "\n";
            return 3;
        }
        fs::path self = selfPath();
        fs::path bak  = self; bak += ".bak";
        fs::path tmp  = self; tmp += ".new";

        // Test writability — system installs (read-only paths) refuse upgrade.
        std::ofstream test(tmp);
        if (!test) {
            std::cerr << "icmg update: install path not writable: " << self.string() << "\n"
                      << "  Use OS package manager or download manually.\n";
            return 4;
        }
        test.close();
        fs::remove(tmp);

        std::cout << "Downloading " << r.asset_url << " -> " << tmp.string() << "\n";
        std::string sh_tmp = tmp.string();
        for (auto& c : sh_tmp) if (c == '\\') c = '/';  // bash-c safe
        std::string cmd = "curl -sL --max-time 120 -o \"" + sh_tmp + "\" \""
                        + r.asset_url + "\"";
        auto res = core::safeExecShell(cmd, false, 130000);
        if (res.exit_code != 0 || !fs::exists(tmp) || fs::file_size(tmp) < 1024) {
            std::cerr << "icmg update: download failed (exit=" << res.exit_code << ")\n";
            fs::remove(tmp);
            return 5;
        }

        // Phase 50 T1: integrity verify before swap.
        if (!verifySha256(tmp, r.asset_url, skip_verify)) {
            fs::remove(tmp);
            return 8;
        }

        std::error_code ec;
        // Backup current.
        fs::remove(bak, ec);                                  // remove old bak
        fs::rename(self, bak, ec);
        if (ec) {
            // Phase 47.x: lock-detected → spawn detached helper that waits
            // for our process to exit, then performs the swap. User gets
            // automatic apply on the next invocation OR after current process
            // terminates (whichever first). Falls back to pending-restart
            // flag if helper spawn fails.
            std::cerr << "icmg update: target locked by current process. "
                      << "Spawning detached helper to apply on exit...\n";
            if (spawnDetachedSwapHelper(self, tmp, bak, r.tag)) {
                std::cerr << "  Helper armed. The new binary will be installed "
                          << "as soon as this process exits.\n"
                          << "  (Auto-confirmed on next `icmg <cmd>` invocation.)\n";
                fs::path pending = self; pending += ".pending-restart";
                std::ofstream pf(pending);
                if (pf) pf << r.tag << "\n" << tmp.string() << "\n";
                return 0;
            }
            // Helper spawn failed — fall back to pending-only flag.
            std::cerr << "  Helper spawn failed. Falling back to pending-restart flag.\n";
            fs::path pending = self; pending += ".pending-restart";
            std::ofstream pf(pending);
            if (pf) {
                pf << r.tag << "\n" << tmp.string() << "\n";
                std::cerr << "  PENDING UPGRADE flagged.\n"
                          << "  Run ANY `icmg <cmd>` in a NEW terminal to complete.\n";
                return 0;
            }
            std::cerr << "  Workaround: taskkill /F /IM icmg.exe (Windows) or "
                      << "killall icmg (Unix), then re-run `icmg update --apply`.\n";
            fs::remove(tmp);
            return 6;
        }
        // Move new in.
        fs::rename(tmp, self, ec);
        if (ec) {
            // Try copy + delete fallback.
            fs::copy_file(tmp, self, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::cerr << "icmg update: install new failed: " << ec.message() << "\n"
                          << "  Restoring .bak. Run `icmg update --rollback` if needed.\n";
                fs::rename(bak, self, ec);
                return 7;
            }
        }
#ifndef _WIN32
        fs::permissions(self,
            fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
            fs::perm_options::add, ec);
#endif
        std::cout << "Installed " << r.tag << ". Old binary kept at " << bak.string() << "\n"
                  << "  Verify: icmg --version\n"
                  << "  Rollback: icmg update --rollback\n\n";

        // Phase 53 T1: per-DLL SHA256 verify against release manifest.
        // Phase 56 T2: auto-rollback on mismatch (opt-out via --no-auto-rollback).
        int dll_mismatch = verifyBundledDlls(self, r, skip_verify);
        if (dll_mismatch > 0 && !no_auto_rollback) {
            std::cerr << "icmg update: AUTO-ROLLBACK triggered ("
                      << dll_mismatch << " DLL(s) failed integrity).\n"
                      << "  Restoring " << bak.string() << " → " << self.string() << "\n";
            std::error_code rec;
            fs::path swap_p = self; swap_p += ".swap";
            fs::rename(self, swap_p, rec);
            fs::rename(bak, self, rec);
            if (!rec) {
                fs::remove(swap_p, rec);
                std::cerr << "  Rollback complete. Re-run `icmg update --apply --skip-verify` to bypass.\n";
                return 9;
            }
            std::cerr << "  Rollback FAILED: " << rec.message() << "\n";
        }

        // Phase 52 T3: auto-refresh installed hooks if .claude/hooks exists in cwd.
        if (fs::exists(fs::current_path() / ".claude" / "hooks")) {
            std::cout << "Refreshing project hooks (.claude/hooks/icmg-*.sh)...\n";
            std::string cmd = "\"" + self.string() + "\" init --install-hooks --force "
                              "--no-agents --no-embedder --no-scan";
            auto res = core::safeExecShell(cmd, false, 15000);
            if (res.exit_code == 0) {
                std::cout << "  Hooks refreshed.\n\n";
            } else {
                std::cerr << "  Hook refresh skipped (exit=" << res.exit_code << ").\n";
            }
        }

        // Phase 44: print release notes + adoption hints so AI agents see new features.
        printReleaseNotes(r.tag);
        return 0;
    }

    // Fetch GitHub release notes for given tag and print as actionable summary.
    void printReleaseNotes(const std::string& tag) {
        std::string url = std::string("https://api.github.com/repos/")
                        + REPO + "/releases/tags/" + tag;
        std::string cmd = "curl -sL --max-time 10 -H \"User-Agent: icmg/"
                        + std::string(CURRENT_VERSION) + "\" \"" + url + "\"";
        auto res = core::safeExecShell(cmd, false, 12000);
        if (res.exit_code != 0 || res.out.empty()) return;
        try {
            auto j = json::parse(res.out);
            std::string body = j.value("body", "");
            std::string name = j.value("name", tag);
            if (body.empty()) return;

            std::cout << "════════════════════════════════════════════════════════════\n"
                      << " WHAT'S NEW in " << name << "\n"
                      << "════════════════════════════════════════════════════════════\n"
                      << body << "\n"
                      << "════════════════════════════════════════════════════════════\n"
                      << "AGENT NOTE: scan above for new commands/flags. Reflexes to\n"
                      << "consider adopting in your next workflow:\n"
                      << "  • Run `icmg --help` to see refreshed command list\n"
                      << "  • Run `icmg <new-cmd> --help` for any unfamiliar names\n"
                      << "  • Update AGENTS.md/CLAUDE.md if new patterns apply here\n"
                      << "  • Run `icmg savings` later to verify ROI of new features\n"
                      << "════════════════════════════════════════════════════════════\n";
        } catch (...) {}
    }

    // Spawn detached helper that waits for current PID to exit, then
    // moves staged .new -> self. Cross-platform: cmd.exe + ping for delay
    // on Windows; sh + sleep on POSIX. Returns true on successful spawn.
    bool spawnDetachedSwapHelper(const fs::path& self, const fs::path& staged,
                                  const fs::path& bak, const std::string& tag) {
#ifdef _WIN32
        DWORD pid = GetCurrentProcessId();
        std::string self_s    = self.string();
        std::string staged_s  = staged.string();
        std::string bak_s     = bak.string();
        std::string pending_s = self_s + ".pending-restart";
        // Build batch script content. Polls process existence; once gone,
        // remove .bak (if any), rename self -> .bak, rename staged -> self,
        // remove .pending-restart marker.
        fs::path script = fs::temp_directory_path() / ("icmg_swap_" + std::to_string(pid) + ".cmd");
        {
            std::ofstream s(script);
            if (!s) return false;
            // Quote paths for Windows.
            s << "@echo off\r\n";
            s << "setlocal\r\n";
            s << ":wait\r\n";
            s << "tasklist /FI \"PID eq " << pid << "\" 2>nul | find \"" << pid << "\" >nul\r\n";
            s << "if not errorlevel 1 (\r\n";
            s << "  ping -n 2 127.0.0.1 >nul\r\n";  // ~1s delay
            s << "  goto wait\r\n";
            s << ")\r\n";
            // Process gone — perform swap.
            s << "if exist \"" << bak_s << "\" del /Q \"" << bak_s << "\"\r\n";
            s << "ren \"" << self_s << "\" \"" << bak.filename().string() << "\" 2>nul\r\n";
            s << "move /Y \"" << staged_s << "\" \"" << self_s << "\" >nul 2>&1\r\n";
            s << "if exist \"" << pending_s << "\" del /Q \"" << pending_s << "\"\r\n";
            s << "del /Q \"%~f0\"\r\n";  // self-delete
        }
        // Spawn detached.
        STARTUPINFOA si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::string cmd = "cmd.exe /c \"\"" + script.string() + "\"\"";
        std::vector<char> mut(cmd.begin(), cmd.end());
        mut.push_back(0);
        BOOL ok = CreateProcessA(nullptr, mut.data(), nullptr, nullptr, FALSE,
                                  DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                                  nullptr, nullptr, &si, &pi);
        if (!ok) return false;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        (void)tag;
        return true;
#else
        // POSIX: simple shell wrapper. PID-based wait via /proc presence.
        pid_t pid = ::getpid();
        fs::path script = fs::temp_directory_path() / ("icmg_swap_" + std::to_string(pid) + ".sh");
        {
            std::ofstream s(script);
            if (!s) return false;
            s << "#!/bin/sh\n";
            s << "while kill -0 " << pid << " 2>/dev/null; do sleep 1; done\n";
            s << "rm -f \"" << bak.string() << "\"\n";
            s << "mv \"" << self.string() << "\" \"" << bak.string() << "\" 2>/dev/null\n";
            s << "mv \"" << staged.string() << "\" \"" << self.string() << "\"\n";
            s << "chmod +x \"" << self.string() << "\"\n";
            s << "rm -f \"" << self.string() << ".pending-restart\"\n";
            s << "rm -- \"$0\"\n";
        }
        chmod(script.c_str(), 0755);
        // Detach via fork + setsid + double-fork.
        pid_t fp = fork();
        if (fp < 0) return false;
        if (fp == 0) {
            setsid();
            if (fork() == 0) {
                execlp("sh", "sh", script.c_str(), nullptr);
                _exit(1);
            }
            _exit(0);
        }
        ::waitpid(fp, nullptr, 0);
        (void)tag;
        return true;
#endif
    }

    // Phase 53 T1: per-DLL integrity verify post-install. Skips silently for
    // missing .sha256 sidecars (transition window). Hard mismatch = warn loudly
    // but does NOT auto-rollback (user can manually rollback if needed).
    int verifyBundledDlls(const fs::path& self, const Release& r, bool skip_verify) {
        if (skip_verify) return 0;
        // Asset URL pattern: replace icmg.exe filename in r.asset_url with each DLL.
        std::string base_url = r.asset_url;
        auto slash = base_url.find_last_of('/');
        if (slash == std::string::npos) return 0;
        base_url = base_url.substr(0, slash + 1);  // ".../v0.35.1/"

        const std::vector<std::string> dlls = {
            "onnxruntime.dll",
            "onnxruntime_providers_shared.dll",
            "libtree-sitter-0.26.dll",
            "wasmtime.dll",
            "libzstd.dll",
            "libwinpthread-1.dll",
        };

        fs::path install_dir = self.parent_path();
        int verified = 0, missing_manifest = 0, mismatch = 0;
        for (auto& dll : dlls) {
            fs::path local = install_dir / dll;
            if (!fs::exists(local)) continue;  // optional, skip
            std::string sha_url = base_url + dll + ".sha256";
            fs::path sha_tmp = install_dir / (dll + ".sha256.tmp");
            std::string sh_path = sha_tmp.string();
            for (auto& c : sh_path) if (c == '\\') c = '/';  // bash-c safe
            std::string cmd = "curl -sL --max-time 10 -o \"" + sh_path
                            + "\" \"" + sha_url + "\"";
            auto res = core::safeExecShell(cmd, false, 12000);
            if (res.exit_code != 0 || !fs::exists(sha_tmp) || fs::file_size(sha_tmp) < 16) {
                ++missing_manifest;
                fs::remove(sha_tmp);
                continue;
            }
            std::ifstream sf(sha_tmp);
            std::string expected;
            sf >> expected;
            sf.close();
            fs::remove(sha_tmp);
            std::transform(expected.begin(), expected.end(), expected.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (expected.size() != 64) continue;
            std::string actual = computeSha256(local);
            if (actual.empty()) continue;
            if (actual != expected) {
                std::cerr << "icmg update: DLL SHA256 MISMATCH — " << dll << "\n"
                          << "  expected: " << expected << "\n"
                          << "  actual:   " << actual << "\n"
                          << "  Run `icmg update --rollback` if you suspect tampering.\n";
                ++mismatch;
            } else {
                ++verified;
            }
        }
        if (verified > 0) {
            std::cout << "icmg update: " << verified << " DLL(s) sha256-verified";
            if (missing_manifest > 0)
                std::cout << " (" << missing_manifest << " manifest not yet uploaded)";
            std::cout << "\n";
        }
        if (mismatch > 0) {
            std::cerr << "icmg update: " << mismatch << " DLL(s) failed integrity check\n";
        }
        return mismatch;
    }

    int doRollback() {
        fs::path self = selfPath();
        fs::path bak  = self; bak += ".bak";
        if (!fs::exists(bak)) {
            std::cerr << "icmg update: no .bak file at " << bak.string() << "\n";
            return 1;
        }
        std::error_code ec;
        fs::path swap = self; swap += ".swap";
        fs::rename(self, swap, ec);
        if (ec) { std::cerr << "rollback rename current -> swap: " << ec.message() << "\n"; return 2; }
        fs::rename(bak, self, ec);
        if (ec) { fs::rename(swap, self, ec); std::cerr << "rollback restore: " << ec.message() << "\n"; return 3; }
        fs::rename(swap, bak, ec);   // current becomes new bak
        std::cout << "Rolled back. Previous (current-before-rollback) saved at " << bak.string() << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("update", UpdateCommand);

// =============================================================================
// `icmg whats-new` — show release notes for current or specific version.
// AI agents call after `icmg update` to learn what changed.
// =============================================================================

class WhatsNewCommand : public BaseCommand {
public:
    std::string name()        const override { return "whats-new"; }
    std::string description() const override {
        return "Show release notes (current version or --since X)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg whats-new [options]\n\n"
            "Options:\n"
            "  --since <ver>   Show notes from <ver> (exclusive) up to current\n"
            "  --tag <ver>     Show notes for one specific version\n"
            "  --json          Machine output\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        std::string since = flagValue(args, "--since");
        std::string tag   = flagValue(args, "--tag");
        bool json_out     = hasFlag(args, "--json");

        if (!tag.empty()) return showOne(tag, json_out);
        if (!since.empty()) return showRange(since, json_out);

        // Default: current version notes.
        return showOne(std::string("v") + CURRENT_VERSION, json_out);
    }

private:
    int showOne(const std::string& tag, bool json_out) {
        std::string url = std::string("https://api.github.com/repos/")
                        + REPO + "/releases/tags/" + tag;
        std::string cmd = "curl -sL --max-time 10 -H \"User-Agent: icmg/"
                        + std::string(CURRENT_VERSION) + "\" \"" + url + "\"";
        auto res = core::safeExecShell(cmd, false, 12000);
        if (res.exit_code != 0 || res.out.empty()) {
            std::cerr << "icmg whats-new: failed to fetch " << tag << "\n";
            return 2;
        }
        try {
            auto j = json::parse(res.out);
            if (json_out) { std::cout << j.dump(2) << "\n"; return 0; }
            std::cout << "═══ " << j.value("name", tag) << " ═══\n"
                      << j.value("body", "") << "\n";
        } catch (...) {
            std::cerr << "icmg whats-new: parse failed\n"; return 3;
        }
        return 0;
    }

    int showRange(const std::string& since, bool json_out) {
        std::string url = std::string("https://api.github.com/repos/")
                        + REPO + "/releases?per_page=20";
        std::string cmd = "curl -sL --max-time 10 -H \"User-Agent: icmg/"
                        + std::string(CURRENT_VERSION) + "\" \"" + url + "\"";
        auto res = core::safeExecShell(cmd, false, 12000);
        if (res.exit_code != 0 || res.out.empty()) {
            std::cerr << "icmg whats-new: failed to fetch releases\n"; return 2;
        }
        try {
            auto j = json::parse(res.out);
            if (!j.is_array()) return 3;
            // Reverse: oldest first within range.
            std::vector<json> in_range;
            for (auto& rel : j) {
                std::string t = rel.value("tag_name", "");
                if (t == since || t == ("v" + since)) break;
                in_range.push_back(rel);
            }
            if (json_out) {
                std::cout << "[\n";
                for (size_t i = 0; i < in_range.size(); ++i) {
                    if (i) std::cout << ",\n";
                    std::cout << in_range[i].dump(2);
                }
                std::cout << "\n]\n";
                return 0;
            }
            std::cout << "Releases since " << since << ":\n\n";
            for (auto it = in_range.rbegin(); it != in_range.rend(); ++it) {
                std::cout << "═══ " << it->value("name", it->value("tag_name", ""))
                          << " ═══\n"
                          << it->value("body", "") << "\n\n";
            }
        } catch (...) {
            std::cerr << "icmg whats-new: parse failed\n"; return 3;
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("whats-new", WhatsNewCommand);

} // namespace icmg::cli
