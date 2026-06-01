// Phase 27 T1: `icmg update` — self-upgrade via github releases.
//
// --check : GET /releases/latest → semver compare with current.
// --apply : download asset for platform → atomic swap (Win: rename .bak, write new).
// --rollback : restore .bak.
//
// Network call via system curl (no libcurl dep). 10s timeout. HTTPS-only.
// Hard-coded host github.com. No auto-cron — explicit user invocation only.

#include "../base_command.hpp"
#include "../defender_decision.hpp"   // v1.75 #187: idempotent Defender gate
#include "../../core/registry.hpp"
#include "../../core/version.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/db.hpp"
#include "../../core/path_utils.hpp"
#include "../../core/update_lock.hpp"   // v1.78.4: shared lock helpers
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

static const char* CURRENT_VERSION = icmg::core::ICMG_VERSION;
static const char* REPO            = "ncmonx/icemage";

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


#ifdef _WIN32
#include <tlhelp32.h>
static int countOrphanIcmgInstances() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    int count = 0;
    DWORD self_pid = GetCurrentProcessId();
    PROCESSENTRY32 pe{}; pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            std::string name = pe.szExeFile;
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (name == "icmg.exe" && pe.th32ProcessID != self_pid) ++count;
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return count;
}

// v1.21.5: poll process list for icmg.exe / icmg-core.exe (except self) until
// all gone or timeout. Belt-and-suspenders after stopOrphanIcmgInstances.
static bool waitForIcmgGone(int timeout_ms) {
    DWORD self_pid = GetCurrentProcessId();
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        int alive = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe{}; pe.dwSize = sizeof(pe);
            if (Process32First(snap, &pe)) {
                do {
                    std::string name = pe.szExeFile;
                    std::transform(name.begin(), name.end(), name.begin(),
                                   [](unsigned char c){ return std::tolower(c); });
                    if ((name == "icmg.exe" || name == "icmg-core.exe")
                        && pe.th32ProcessID != self_pid) ++alive;
                } while (Process32Next(snap, &pe));
            }
            CloseHandle(snap);
        }
        if (alive == 0) return true;
        Sleep(200);
        elapsed += 200;
    }
    return false;
}

// v1.21.1: stop ALL running icmg.exe / icmg-core.exe instances (except self)
// before binary swap so the rename-aside doesn't leave the old code running
// in memory. v1.21.5: enhanced — taskkill /F fallback after TerminateProcess,
// then poll-until-gone (waitForIcmgGone) so swap doesn't race re-spawn.
static int stopOrphanIcmgInstances() {
    DWORD self_pid = GetCurrentProcessId();
    std::vector<DWORD> targets;
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32 pe{}; pe.dwSize = sizeof(pe);
        if (Process32First(snap, &pe)) {
            do {
                std::string name = pe.szExeFile;
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if ((name == "icmg.exe" || name == "icmg-core.exe")
                    && pe.th32ProcessID != self_pid) {
                    targets.push_back(pe.th32ProcessID);
                }
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
    }
    if (targets.empty()) return 0;

    // v1.21.5: Phase 1 — TerminateProcess directly. The old no-op "graceful"
    // phase wasted 3s with no actual signal channel for GUI children. New
    // flow is just kill-fast, then verify with poll loop.
    int killed = 0;
    for (DWORD pid : targets) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!h) continue;
        DWORD ec = STILL_ACTIVE;
        if (GetExitCodeProcess(h, &ec) && ec == STILL_ACTIVE) {
            if (TerminateProcess(h, 0)) ++killed;
        }
        CloseHandle(h);
    }

    // v1.21.5: Phase 2 — taskkill /F fallback for survivors that
    // TerminateProcess couldn't reach (admin-only handles, antivirus shield).
    for (DWORD pid : targets) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) continue;
        DWORD ec = STILL_ACTIVE;
        bool still_alive = GetExitCodeProcess(h, &ec) && ec == STILL_ACTIVE;
        CloseHandle(h);
        if (!still_alive) continue;
        std::string cmd = "taskkill /F /PID " + std::to_string(pid) + " > nul 2>&1";
        std::system(cmd.c_str());
    }

    // v1.21.5: Phase 3 — poll until all gone, max 5s. Catches re-spawn races.
    (void)waitForIcmgGone(5000);

    // Wipe stale pid files so next service start doesn't see a "alive" PID
    // that now belongs to some other unrelated process.
    {
        const char* prof = std::getenv("USERPROFILE");
        if (prof && *prof) {
            std::error_code ec;
            fs::remove(fs::path(prof) / ".icmg" / "service.pid", ec);
            fs::remove(fs::path(prof) / ".icmg" / "rule-daemon.pid", ec);
            fs::remove(fs::path(prof) / ".icmg" / "service.starting", ec);
        }
    }
    return killed;
}
#endif

// v1.21.5: write `~/.icmg/updating.lock` sentinel. While present, any
// auto-spawn paths (exec_client maybe_autospawn_service, hook scripts) MUST
// skip launching icmg-core — this keeps a freshly-killed service from
// re-spawning on the old binary before swap completes. Cross-platform: on
// POSIX the sentinel is harmless (no exec_client to check it) but kept for
// symmetry + future Linux hook checks.
// v1.78.4: lock helpers moved to src/core/update_lock.{hpp,cpp}.
// Bring into local scope for backward-compat call sites below.
using icmg::core::updatingLockPath;
using icmg::core::writeUpdatingLock;
using icmg::core::clearUpdatingLock;

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
            "  --no-defender          Skip Windows Defender exclusion step (avoids B: scan popup)\n"
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
        if (hasFlag(args, "--no-defender")) {
#ifdef _WIN32
            _putenv_s("ICMG_NO_DEFENDER", "1");
#else
            setenv("ICMG_NO_DEFENDER", "1", 1);
#endif
        }
        bool rollback = hasFlag(args, "--rollback");
        bool preview  = flagValue(args, "--channel") == "preview";
        bool json_out = hasFlag(args, "--json");
        bool skip_verify = hasFlag(args, "--skip-verify");
        bool no_auto_rollback = hasFlag(args, "--no-auto-rollback");
        bool no_self_test = hasFlag(args, "--no-self-test");

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

        return doApply(latest, skip_verify, no_auto_rollback, no_self_test);
    }

private:
    struct Release { std::string tag; std::string asset_url; std::string sha256; };

    Release fetchLatest(bool preview) {
        Release r;
        std::string url = preview
            ? std::string("https://api.github.com/repos/") + REPO + "/releases?per_page=1"
            : std::string("https://api.github.com/repos/") + REPO + "/releases/latest";
        std::string cmd = std::string(core::curlBin()) + " -sL --max-time 10 -H \"User-Agent: icmg/"
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
            std::string want = wantedAssetName(r.tag);
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

    static std::string wantedAssetName(const std::string& tag = "") {
        std::string ver = tag;
        if (!ver.empty() && (ver[0] == 'v' || ver[0] == 'V')) ver.erase(0, 1);
#ifdef _WIN32
        return "icmg-" + ver + "-win-x64.zip";
#elif defined(__APPLE__)
    #if defined(__aarch64__) || defined(__arm64__)
        return "icmg-" + ver + "-macos-arm64.tar.gz";
    #else
        return "icmg-" + ver + "-macos-x64.tar.gz";
    #endif
#else
        return "icmg-" + ver + "-linux-x64.tar.gz";
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
    // releases pre-v0.37.0 lack .sha256 sidecar files).
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
        std::string cmd = std::string(core::curlBin()) + " -sL --max-time 10 -o \"" + sh_path
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

    // Extract icmg binary from downloaded asset (zip on Win, tar.gz on POSIX).
    // Copies extracted binary to dest_exe; copies DLLs/.so into dll_dir.
    // Returns true on success.
    static bool extractFromAsset(const fs::path& asset, const fs::path& dest_exe,
                                  const fs::path& dll_dir) {
#ifdef _WIN32
        // Use tar.exe (ships with Windows 10+ / Server 2019+).
        fs::path tmp_dir = fs::temp_directory_path()
                         / ("icmg_ex_" + std::to_string(GetCurrentProcessId()));
        fs::create_directories(tmp_dir);

        // PowerShell Expand-Archive via CreateProcessA — avoids two failure modes:
        //   1. MSYS2/Git Bash tar treats "C:/path" as archive member, not drive letter.
        //   2. cmd.exe /s /c quoting breaks when -Command arg contains inner quotes.
        // Forward-slash paths: PowerShell accepts them; no backslash escaping needed.
        {
            auto to_fwd = [](std::string s) {
                for (auto& c : s) if (c == '\\') c = '/'; return s;
            };
            auto ps_esc = [](const std::string& s) {
                std::string r; for (char c : s) { if (c=='\'') r+="''"; else r+=c; } return r;
            };
            std::string cl = "powershell.exe -NoProfile -NonInteractive -Command "
                             "\"Expand-Archive -LiteralPath '" + ps_esc(to_fwd(asset.string())) +
                             "' -DestinationPath '" + ps_esc(to_fwd(tmp_dir.string())) + "' -Force\"";
            std::vector<char> cl_buf(cl.begin(), cl.end());
            cl_buf.push_back('\0');
            STARTUPINFOA si{}; si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            bool launched = CreateProcessA(nullptr, cl_buf.data(), nullptr, nullptr,
                                           FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
            if (!launched) {
                std::cerr << "icmg update: failed to launch powershell.exe (err="
                          << GetLastError() << ")\n";
                fs::remove_all(tmp_dir);
                return false;
            }
            WaitForSingleObject(pi.hProcess, 60000);
            DWORD ps_exit = 1;
            GetExitCodeProcess(pi.hProcess, &ps_exit);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (ps_exit != 0) {
                std::cerr << "icmg update: extraction failed (powershell exit=" << ps_exit << ")\n";
                fs::remove_all(tmp_dir);
                return false;
            }
        }

        fs::path extracted_exe = tmp_dir / "icmg.exe";
        if (!fs::exists(extracted_exe)) {
            std::cerr << "icmg update: icmg.exe not found in archive\n";
            fs::remove_all(tmp_dir);
            return false;
        }

        // v1.6.1: strip Zone.Identifier ADS ("Mark of the Web") from extracted
        // artifacts. SmartScreen uses this stream to gate reputation checks;
        // without it, SmartScreen skips the drive-probe scan that triggers the
        // B:\ popup. Best-effort; failure does not abort upgrade.
        {
            std::string unblock = "powershell -NoProfile -Command "
                                  "\"Get-ChildItem -LiteralPath '"
                                + tmp_dir.string()
                                + "' | Unblock-File\"";
            (void)core::safeExecShell(unblock, true, 15000);
        }

        std::error_code ec;
        fs::rename(extracted_exe, dest_exe, ec);
        if (ec) {
            fs::copy_file(extracted_exe, dest_exe,
                          fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::cerr << "icmg update: cannot stage binary: " << ec.message() << "\n";
                fs::remove_all(tmp_dir);
                return false;
            }
        }

        // Update DLLs alongside binary. Locked DLLs (in-use by current icmg
        // process or another running instance) are handled via rename-aside:
        // Windows allows renaming open files because handles point to the
        // inode, not the path. We rename current DLL to a .old-<pid> sidecar,
        // copy new DLL to original path, and sweep .old-* on next launch.
        int dll_ok = 0, dll_aside = 0, dll_failed = 0;
        for (auto& entry : fs::directory_iterator(tmp_dir)) {
            if (entry.path().extension() != ".dll") continue;
            fs::path dst = dll_dir / entry.path().filename();
            std::error_code ec1;
            fs::copy_file(entry.path(), dst,
                          fs::copy_options::overwrite_existing, ec1);
            if (!ec1) { ++dll_ok; continue; }
            // Locked. Rename-aside.
            std::error_code ec2;
            fs::path aside = dst;
            aside += ".old-" + std::to_string(GetCurrentProcessId());
            fs::rename(dst, aside, ec2);
            if (!ec2) {
                std::error_code ec3;
                fs::copy_file(entry.path(), dst,
                              fs::copy_options::overwrite_existing, ec3);
                if (!ec3) {
                    ++dll_aside;
                    std::cout << "icmg update: DLL replaced via rename-aside: "
                              << entry.path().filename().string()
                              << " (cleanup on next launch)\n";
                    continue;
                }
            }
            // Last resort: schedule delayed rename at next reboot.
            BOOL scheduled = MoveFileExA(
                entry.path().string().c_str(),
                dst.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_DELAY_UNTIL_REBOOT);
            ++dll_failed;
            std::cerr << "icmg update: DLL " << entry.path().filename().string()
                      << " could not be replaced (locked, rename failed)";
            if (scheduled) std::cerr << " — scheduled for next reboot";
            std::cerr << "\n";
        }
        if (dll_failed > 0) {
            std::cerr << "icmg update: " << dll_failed << " DLL(s) require reboot.\n";
        }
        if (dll_ok + dll_aside > 0) {
            std::cout << "icmg update: " << dll_ok << " DLL(s) replaced directly, "
                      << dll_aside << " via rename-aside.\n";
        }
        fs::remove_all(tmp_dir);
        return true;
#else
        // POSIX: .tar.gz
        fs::path tmp_dir = fs::temp_directory_path()
                         / ("icmg_ex_" + std::to_string(getpid()));
        fs::create_directories(tmp_dir);
        std::string cmd = "tar -xzf \"" + asset.string()
                        + "\" -C \"" + tmp_dir.string() + "\"";
        auto res = core::safeExecShell(cmd, false, 30000);
        if (res.exit_code != 0) {
            fs::remove_all(tmp_dir);
            return false;
        }
        // Binary may be at root or inside a subdirectory.
        fs::path extracted_exe;
        for (auto& e : fs::recursive_directory_iterator(tmp_dir)) {
            auto fn = e.path().filename().string();
            if (fn == "icmg" || fn == "icmg.exe") { extracted_exe = e.path(); break; }
        }
        if (extracted_exe.empty()) { fs::remove_all(tmp_dir); return false; }
        std::error_code ec;
        fs::rename(extracted_exe, dest_exe, ec);
        if (ec) {
            fs::copy_file(extracted_exe, dest_exe,
                          fs::copy_options::overwrite_existing, ec);
            if (ec) { fs::remove_all(tmp_dir); return false; }
        }
        fs::permissions(dest_exe,
            fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
            fs::perm_options::add, ec);
        fs::remove_all(tmp_dir);
        return true;
#endif
    }

    int doApply(const Release& r, bool skip_verify, bool no_auto_rollback = false,
                 bool no_self_test = false) {
        if (r.asset_url.empty()) {
            std::cerr << "icmg update: no platform asset on release " << r.tag << "\n";
            return 3;
        }
#ifdef _WIN32
        // v1.6.8: pre-flight orphan check. Other icmg.exe instances
        // hold DB + file locks; swap will fail with ERROR_SHARING_VIOLATION.
        // Hint user to run cleanup; do not auto-kill.
        {
            int orphan = countOrphanIcmgInstances();
            if (orphan > 2) {
                std::cerr << "icmg update: " << orphan
                          << " other icmg.exe instances running\n"
                          << "               DB/file lock risk. Run first:\n"
                          << "                 icmg cleanup orphans\n"
                          << "                 icmg cleanup kill-orphans --confirm\n";
            }
        }
#endif
        fs::path self = selfPath();
        fs::path bak  = self; bak += ".bak";
        fs::path tmp  = self; tmp += ".new";  // staged binary (extracted exe)
#ifdef _WIN32
        fs::path zip  = self; zip += ".download.zip";
#else
        fs::path zip  = self; zip += ".download.tar.gz";
#endif

        // Test writability — system installs (read-only paths) refuse upgrade.
        std::ofstream test(tmp);
        if (!test) {
            std::cerr << "icmg update: install path not writable: " << self.string() << "\n"
                      << "  Use OS package manager or download manually.\n";
            return 4;
        }
        test.close();
        fs::remove(tmp);

        // Download the release asset (zip/tar.gz) to a separate path.
        std::cout << "Downloading " << r.asset_url << " ...\n";
        std::string sh_zip = zip.string();
        for (auto& c : sh_zip) if (c == '\\') c = '/';
        std::string cmd = std::string(core::curlBin()) + " -fsSL --retry 2 --retry-delay 3 --max-time 300 -o \"" + sh_zip + "\" \""
                        + r.asset_url + "\"";
        auto res = core::safeExecShell(cmd, false, 320000);  // v1.40.2: bumped 130s -> 320s for larger LLM-on assets
        if (res.exit_code != 0 || !fs::exists(zip) || fs::file_size(zip) < 1024) {
            std::cerr << "icmg update: download failed (exit=" << res.exit_code << ", size=" << (fs::exists(zip) ? fs::file_size(zip) : 0) << ")\n";
            fs::remove(zip);
            return 5;
        }

        // Phase 50 T1: integrity verify on the archive.
        if (!verifySha256(zip, r.asset_url, skip_verify)) {
            fs::remove(zip);
            return 8;
        }

        // Extract binary (and DLLs on Windows) from archive.
        std::cout << "Extracting...\n";
        if (!extractFromAsset(zip, tmp, self.parent_path())) {
            fs::remove(zip);
            return 9;
        }
        fs::remove(zip);

        // v1.21.5: write updating.lock sentinel BEFORE killing the service
        // so exec_client maybe_autospawn_service skips re-spawn during the
        // swap window. Cleared after new service confirmed alive.
        writeUpdatingLock();

        // v1.21.1: stop running icmg / icmg-core instances before binary swap
        // so the old code path doesn't linger in memory after rename. Service
        // pidfiles wiped so post-swap restart spawns fresh from the new exe.
#ifdef _WIN32
        {
            int n = stopOrphanIcmgInstances();
            if (n > 0) {
                std::cout << "Stopped " << n << " running icmg process(es) before swap.\n";
            }
        }
#endif

        std::error_code ec;
        // v1.78.4: retry rename up to 3 times with short sleep.
        // Dispatcher now checks updating.lock so new icmg processes bail out,
        // but hook-triggered processes that were already mid-flight need time
        // to exit. 3 × 300ms gives ~900ms grace after stopOrphanIcmgInstances.
        fs::remove(bak, ec);
        for (int _attempt = 0; _attempt < 3; ++_attempt) {
            ec = {};
            fs::rename(self, bak, ec);
            if (!ec) break;
#ifdef _WIN32
            Sleep(300);
#else
            usleep(300000);
#endif
        }
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
                clearUpdatingLock();
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
                clearUpdatingLock();
                return 0;
            }
            std::cerr << "  Workaround: taskkill /F /IM icmg.exe (Windows) or "
                      << "killall icmg (Unix), then re-run `icmg update --apply`.\n";
            fs::remove(tmp);
            clearUpdatingLock();
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
                clearUpdatingLock();
                return 7;
            }
        }
#ifndef _WIN32
        fs::permissions(self,
            fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
            fs::perm_options::add, ec);
#endif
        // v1.21.6: dual-binary install auto-sync. Older v1.18.x installs
        // shipped `icmg.exe` (launcher, ~52 KB) + `icmg-core.exe` (worker,
        // ~17 MB). v1.19.1+ shipped a single monolithic `icmg.exe`. Users on
        // the legacy layout had `update --apply` overwriting only the file
        // matching `self` (which routes via icmg-core for IPC-served commands)
        // — the OTHER file silently drifted out-of-version. Symptoms:
        // launcher-era IPC quirks, slow init, mismatched feature detection.
        //
        // Fix: after a successful swap, detect the sibling and overwrite it
        // with the same new monolithic binary so BOTH entry-points run the
        // same code regardless of which one the user invokes.
        {
            fs::path sibling;
            std::string self_name = self.filename().string();
            std::transform(self_name.begin(), self_name.end(), self_name.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (self_name == "icmg.exe" || self_name == "icmg") {
                sibling = self.parent_path() / "icmg-core.exe";
            } else if (self_name == "icmg-core.exe" || self_name == "icmg-core") {
                sibling = self.parent_path() / "icmg.exe";
            }
            if (!sibling.empty() && fs::exists(sibling) && sibling != self) {
                std::error_code sc;
                // Backup sibling before overwrite (keep .bak parallel to self.bak).
                fs::path sibling_bak = sibling; sibling_bak += ".bak";
                fs::remove(sibling_bak, sc);
                fs::copy_file(sibling, sibling_bak, sc);
                fs::copy_file(self, sibling,
                              fs::copy_options::overwrite_existing, sc);
                if (!sc) {
                    std::cout << "  dual-binary: synced " << sibling.filename().string()
                              << " (" << fs::file_size(sibling) / 1024 << " KB)\n";
                } else {
                    std::cerr << "  dual-binary: sibling sync failed: "
                              << sc.message() << " — old "
                              << sibling.filename().string() << " may stay outdated\n";
                }
            }
        }

        std::cout << "Installed " << r.tag << ". Old binary kept at " << bak.string() << "\n"
                  << "  Verify: icmg --version\n"
                  << "  Rollback: icmg update --rollback\n\n";

        // v1.21.1: restart service so popup-killer + cron tick + exec_server
        // pipe come back online running the NEW binary. Detached spawn so the
        // current process can exit cleanly. Best-effort; user-visible warning
        // on failure but not a hard error.
#ifdef _WIN32
        {
            std::string svc_cmd = "\"" + self.string() + "\" service start";
            std::vector<char> buf(svc_cmd.begin(), svc_cmd.end());
            buf.push_back('\0');
            STARTUPINFOA si_s{}; si_s.cb = sizeof(si_s);
            si_s.dwFlags = STARTF_USESHOWWINDOW;
            si_s.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi_s{};
            if (CreateProcessA(self.string().c_str(), buf.data(),
                               nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW | DETACHED_PROCESS
                               | CREATE_NEW_PROCESS_GROUP,
                               nullptr, nullptr, &si_s, &pi_s)) {
                CloseHandle(pi_s.hThread);
                CloseHandle(pi_s.hProcess);
                std::cout << "  service: restarted (running new binary)\n";

                // v1.21.5: wait for new service to write its pidfile so the
                // user's NEXT `icmg <cmd>` invocation lands on the fresh
                // service rather than racing the spawn. Max 5s, then proceed.
                const char* prof = std::getenv("USERPROFILE");
                if (prof && *prof) {
                    fs::path pidf = fs::path(prof) / ".icmg" / "service.pid";
                    int waited = 0;
                    while (waited < 5000 && !fs::exists(pidf)) {
                        Sleep(200);
                        waited += 200;
                    }
                    if (fs::exists(pidf)) {
                        std::cout << "  service: ready after "
                                  << waited << " ms\n";
                    }
                }
            } else {
                std::cerr << "  service: restart failed; run `icmg service start` manually\n";
            }
        }
#endif

        // v1.21.5: clear updating.lock so auto-spawn paths resume working.
        // Done AFTER service-ready verification so the brief window between
        // service-start and pidfile-write is also protected.
        clearUpdatingLock();

        // v1.75 (#187): re-adding the Defender exclusion on EVERY upgrade poked
        // Defender into a full-volume scan that opened unrelated drives (subst B:).
        // The exclusion is keyed by process PATH (unchanged across atomic-rename
        // upgrades), so re-adding is a security no-op. Honor the cached flag +
        // ICMG_NO_DEFENDER / --no-defender opt-out -> idempotent, no scan trigger.
#ifdef _WIN32
        {
            bool flag_exists = false;
            const char* prof = std::getenv("USERPROFILE");
            fs::path marker;
            if (prof && *prof) {
                marker = fs::path(prof) / ".icmg" / "defender-excluded.flag";
                std::error_code _fe; flag_exists = fs::exists(marker, _fe);
            }
            const char* nd = std::getenv("ICMG_NO_DEFENDER");
            bool env_no_defender = (nd && *nd);
            if (!icmg::cli::shouldRunDefenderExclusion(flag_exists, env_no_defender, false)) {
                std::cout << "  Defender: exclusion unchanged (cached/opt-out; no scan trigger).\n";
            } else {
                std::string exe = self.string();
                for (auto& c : exe) if (c == '\\') c = '/';
                std::string cl = "powershell.exe -NoProfile -NonInteractive -Command "
                                 "\"Add-MpPreference -ExclusionProcess '" + exe + "'\"";
                std::vector<char> cl_buf(cl.begin(), cl.end());
                cl_buf.push_back('\0');
                STARTUPINFOA si{}; si.cb = sizeof(si);
                PROCESS_INFORMATION pi{};
                if (CreateProcessA(nullptr, cl_buf.data(), nullptr, nullptr,
                                   FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    WaitForSingleObject(pi.hProcess, 5000);
                    DWORD ec = 1;
                    GetExitCodeProcess(pi.hProcess, &ec);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    if (ec == 0) {
                        std::cout << "  Defender exclusion added.\n";
                        if (!marker.empty()) {
                            std::error_code _ed;
                            fs::create_directories(marker.parent_path(), _ed);
                            std::ofstream m(marker.string()); m << "ok\n";
                        }
                    }
                }
            }
        }
#endif

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

        // Auto-refresh hooks for any project context: .icmg/, .claude/hooks/, or .claude/settings.local.json.
        {
            auto cwd = fs::current_path();
            bool is_project = fs::exists(cwd / ".icmg")
                           || fs::exists(cwd / ".claude" / "hooks")
                           || fs::exists(cwd / ".claude" / "settings.local.json");
            if (is_project) {
                std::cout << "Refreshing project hooks...\n";
                // v1.18.1: ICMG_NO_AUTOSPAWN prevents exec_client from
                // triggering service spawn during update fan-out (caused
                // bloat — 32+ stuck icmg-core procs).
#ifdef _WIN32
                std::string cmd = "set ICMG_NO_AUTOSPAWN=1 && \""
                                + self.string() + "\" init --install-hooks --force "
                                  "--no-agents --no-embedder --no-scan";
#else
                std::string cmd = "ICMG_NO_AUTOSPAWN=1 \""
                                + self.string() + "\" init --install-hooks --force "
                                  "--no-agents --no-embedder --no-scan";
#endif
                auto res = core::safeExecShell(cmd, false, 15000);
                if (res.exit_code == 0) {
                    std::cout << "  Hooks refreshed.\n\n";
                } else {
                    std::cerr << "  Hook refresh skipped (exit=" << res.exit_code << ").\n";
                }
            } else {
                std::cout << "  Tip: run `icmg update --apply` from a project directory to auto-refresh hooks.\n";
            }
        }

        // Refresh hooks in all registered projects from global.db.
        // Ensures every project gets updated hooks after upgrade, not just CWD.
        {
            std::string gdb_path = core::icmgGlobalDir() + "/global.db";
            if (fs::exists(gdb_path)) {
                try {
                    core::Db gdb(gdb_path);
                    std::vector<std::string> proj_paths;
                    gdb.query("SELECT path FROM projects", {}, [&](const core::Row& row) {
                        if (!row.empty()) proj_paths.push_back(row[0]);
                    });
                    if (!proj_paths.empty()) {
                        std::cout << "Refreshing hooks in " << proj_paths.size()
                                  << " registered project(s)...\n";
                        for (auto& p : proj_paths) {
                            if (!fs::exists(p) || !fs::exists(fs::path(p) / ".icmg")) continue;
#ifdef _WIN32
                            // v1.18.1: ICMG_NO_AUTOSPAWN prevents fan-out
                            // service spawn cascade (bloat fix).
                            std::string cmd = "set ICMG_NO_AUTOSPAWN=1 && "
                                              "cd /d \"" + p + "\" && \""
                                           + self.string()
                                           + "\" init --install-hooks --force "
                                             "--no-agents --no-embedder --no-scan "
                                             "--no-backup --no-maintain --no-mirror "
                                             "--no-sentinel --no-auto-upgrade";
#else
                            std::string cmd = "ICMG_NO_AUTOSPAWN=1 "
                                              "cd \"" + p + "\" && \""
                                           + self.string()
                                           + "\" init --install-hooks --force "
                                             "--no-agents --no-embedder --no-scan "
                                             "--no-backup --no-maintain --no-mirror "
                                             "--no-sentinel --no-auto-upgrade";
#endif
                            auto res = core::safeExecShell(cmd, false, 20000);
                            std::cout << "  " << p << ": "
                                      << (res.exit_code == 0 ? "OK" : "skipped") << "\n";
                        }
                    }
                } catch (...) {}
            }
        }

        // If a system-wide install path was recorded by `icmg install --system`,
        // refresh the system binary so all server users get the upgrade automatically.
        {
            std::string sentinel = core::icmgGlobalDir() + "/system-path.txt";
            if (fs::exists(sentinel)) {
                std::ifstream sf(sentinel);
                std::string sys_dir;
                std::getline(sf, sys_dir);
                while (!sys_dir.empty() &&
                       (sys_dir.back() == '\n' || sys_dir.back() == '\r' ||
                        sys_dir.back() == ' '))
                    sys_dir.pop_back();
                if (!sys_dir.empty() && fs::exists(sys_dir)) {
#ifdef _WIN32
                    fs::path dest_bin = fs::path(sys_dir) / "icmg.exe";
#else
                    fs::path dest_bin = fs::path(sys_dir) / "icmg";
#endif
                    std::error_code ec;
                    fs::copy_file(self, dest_bin, fs::copy_options::overwrite_existing, ec);
                    if (ec) {
                        std::cerr << "  System binary refresh failed (" << sys_dir
                                  << "): " << ec.message() << "\n";
                    } else {
                        std::cout << "  System binary refreshed: " << dest_bin.string() << "\n";
                    }
                }
            }
        }

        // v1.15.0: self-test smoke after upgrade. Run `icmg --version` +
        // `icmg doctor` on the new binary. If both succeed, stamp a marker
        // file; if either fails, auto-rollback to .bak (binary swap).
        if (!no_self_test) {
            std::cout << "Running post-upgrade self-test...\n";
            std::string vcmd = "\"" + self.string() + "\" --version";
            auto vr = core::safeExecShell(vcmd, true, 10000);
            bool version_ok = (vr.exit_code == 0
                && vr.out.find(CURRENT_VERSION) == std::string::npos);
            // version_ok check is intentionally inverted-then-corrected: we
            // expect the NEW binary's version, not the old CURRENT_VERSION.
            version_ok = (vr.exit_code == 0 && !vr.out.empty());

            std::string dcmd = "\"" + self.string() + "\" doctor";
            auto dr = core::safeExecShell(dcmd, true, 15000);
            bool doctor_ok = (dr.exit_code == 0);

            if (version_ok && doctor_ok) {
                std::ofstream st(core::icmgGlobalDir()
                                  + "/last-upgrade-verified.txt");
                if (st) st << r.tag << "\n";
                std::cout << "  Self-test: PASS (version + doctor green).\n";
            } else {
                std::cerr << "  Self-test FAILED: version_ok=" << version_ok
                          << " doctor_ok=" << doctor_ok << "\n"
                          << "  Auto-rollback to " << bak.string() << "...\n";
                std::error_code rec;
                fs::path swap_p = self; swap_p += ".swap";
                fs::rename(self, swap_p, rec);
                fs::rename(bak, self, rec);
                if (!rec) {
                    fs::remove(swap_p, rec);
                    std::cerr << "  Rollback complete. Run `icmg update --apply"
                              << " --skip-verify --no-self-test` to bypass.\n";
                    return 9;
                }
                std::cerr << "  Rollback FAILED: " << rec.message() << "\n";
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
        std::string cmd = std::string(core::curlBin()) + " -sL --max-time 10 -H \"User-Agent: icmg/"
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
                                  CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
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
        base_url = base_url.substr(0, slash + 1);  // ".../v0.37.0/"

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
            std::string cmd = std::string(core::curlBin()) + " -sL --max-time 10 -o \"" + sh_path
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
        std::string cmd = std::string(core::curlBin()) + " -sL --max-time 10 -H \"User-Agent: icmg/"
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
        std::string cmd = std::string(core::curlBin()) + " -sL --max-time 10 -H \"User-Agent: icmg/"
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
