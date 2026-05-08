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
#ifdef _WIN32
  #include <windows.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

static const char* CURRENT_VERSION = "0.14.1";   // keep synced with main.cpp / mcp/server.cpp
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

        return doApply(latest, skip_verify);
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

    int doApply(const Release& r, bool /*skip_verify*/) {
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
        std::string cmd = "curl -sL --max-time 120 -o \"" + tmp.string() + "\" \""
                        + r.asset_url + "\"";
        auto res = core::safeExecShell(cmd, false, 130000);
        if (res.exit_code != 0 || !fs::exists(tmp) || fs::file_size(tmp) < 1024) {
            std::cerr << "icmg update: download failed (exit=" << res.exit_code << ")\n";
            fs::remove(tmp);
            return 5;
        }
        std::error_code ec;
        // Backup current.
        fs::remove(bak, ec);                                  // remove old bak
        fs::rename(self, bak, ec);
        if (ec) {
            std::cerr << "icmg update: rename current -> .bak failed: " << ec.message() << "\n";
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
                  << "  Rollback: icmg update --rollback\n";
        return 0;
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

} // namespace icmg::cli
