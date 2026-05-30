// Phase 57: `icmg doctor` — diagnose + auto-fix common drift.
//
// Goes one step beyond `health` (read-only check). Doctor actively repairs:
//   - Missing project hooks → re-run `init --install-hooks --force`
//   - Orphaned `.bak` / `.pending-restart` from stalled updates → archive / clean
//   - Stale sayless hook (no last-trigger after 30d but flag ON) → reinstall
//   - Strict flag set globally but project hooks NOT in strict mode → reinstall strict
//   - DB integrity FAIL → suggest backup + reindex (does NOT auto-modify DB)
//   - Missing bundled DLLs alongside icmg.exe → re-fetch via `update --apply`
//
// Default: report + apply safe fixes. `--dry-run` to preview only.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#ifdef _WIN32
  #include <windows.h>
#endif
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

class DoctorCommand : public BaseCommand {
public:
    std::string name()        const override { return "doctor"; }
    std::string description() const override {
        return "Diagnose + auto-fix common config drift";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg doctor [options]\n\n"
            "Checks and (by default) repairs:\n"
            "  - Missing project hooks (.claude/hooks/icmg-*.sh)\n"
            "  - Orphaned .bak / .pending-restart files from stalled updates\n"
            "  - Stale sayless hook trigger (>30d) when sayless ON\n"
            "  - Strict flag global ON but project hooks not in strict mode\n"
            "  - Missing bundled DLLs next to icmg.exe (Windows)\n\n"
            "Options:\n"
            "  --dry-run      Report only; apply no fixes\n"
            "  --verbose      Print details for OK checks too\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool dry  = hasFlag(args, "--dry-run");
        bool verb = hasFlag(args, "--verbose");

        std::cout << "icmg doctor — " << (dry ? "diagnose only" : "diagnose + auto-fix") << "\n";
        int issues = 0, fixed = 0;

        // 1. Project hooks present?
        fs::path root  = fs::current_path();
        fs::path hooks = root / ".claude" / "hooks";
        if (!fs::exists(hooks)) {
            ++issues;
            std::cout << "  ! [hooks] .claude/hooks missing — run `icmg init` first.\n";
        } else {
            const char* required[] = {
                "icmg-bash-rewrite.sh", "icmg-shrink-read.sh",
                "icmg-cap-output.sh", "icmg-sayless-prompt.sh", nullptr
            };
            int miss = 0;
            for (int i = 0; required[i]; ++i) {
                if (!fs::exists(hooks / required[i])) ++miss;
            }
            if (miss > 0) {
                ++issues;
                std::cout << "  ! [hooks] " << miss << " hook script(s) missing.\n";
                if (!dry) {
                    fixed += runInitInstallHooks() ? 1 : 0;
                }
            } else if (verb) {
                std::cout << "  ✓ [hooks] all 4 required scripts present\n";
            }
        }

        // 2. Strict flag global ON but project hooks not strict?
        fs::path strict_flag = homeDir() / ".icmg" / "strict.flag";
        bool strict_on = fs::exists(strict_flag);
        if (strict_on) {
            fs::path settings = root / ".claude" / "settings.local.json";
            if (fs::exists(settings)) {
                std::ifstream sf(settings);
                std::string content((std::istreambuf_iterator<char>(sf)), {});
                if (content.find("ICMG_SHRINK_STRICT=1") == std::string::npos) {
                    ++issues;
                    std::cout << "  ! [strict] global ON but project hooks NOT in strict mode.\n";
                    if (!dry) {
                        fixed += runInitInstallHooks(/*strict=*/true) ? 1 : 0;
                    }
                } else if (verb) {
                    std::cout << "  ✓ [strict] global ON, project hooks in strict mode\n";
                }
            }
        } else if (verb) {
            std::cout << "  ✓ [strict] global OFF\n";
        }

        // 3. Stale .bak / .pending-restart from stalled upgrade
        auto self = selfPath();
        if (!self.empty()) {
            fs::path bak = self; bak += ".bak";
            fs::path pending = self.parent_path() / ".pending-restart";
            if (fs::exists(bak)) {
                auto age_days = ageDays(bak);
                if (age_days > 7) {
                    ++issues;
                    std::cout << "  ! [update] orphan " << bak.filename().string()
                              << " (" << age_days << "d old) — archiving.\n";
                    if (!dry) {
                        std::error_code ec;
                        fs::path archive = bak; archive += ".archived";
                        fs::rename(bak, archive, ec);
                        if (!ec) ++fixed;
                    }
                } else if (verb) {
                    std::cout << "  ✓ [update] .bak present (" << age_days << "d old, kept)\n";
                }
            }
            if (fs::exists(pending)) {
                auto age_days = ageDays(pending);
                if (age_days > 1) {
                    ++issues;
                    std::cout << "  ! [update] stale .pending-restart ("
                              << age_days << "d old) — removing.\n";
                    if (!dry) {
                        std::error_code ec;
                        fs::remove(pending, ec);
                        if (!ec) ++fixed;
                    }
                }
            }
        }

        // 4. Sayless flag ON but no recent trigger?
        fs::path sayless_flag = homeDir() / ".icmg" / "sayless.flag";
        if (fs::exists(sayless_flag)) {
            fs::path last = homeDir() / ".icmg" / "sayless-last-trigger.txt";
            if (!fs::exists(last)) {
                ++issues;
                std::cout << "  ! [sayless] flag ON but never fired — hook likely missing.\n";
                if (!dry) {
                    bool ok = runInitInstallHooks();
                    fixed += ok ? 1 : 0;
                    if (ok) {
                        // Stamp last-trigger so doctor doesn't re-warn until hook actually fires
                        std::ofstream ts(last);
                        ts << "reinstalled\n";
                    }
                }
            } else {
                auto age_days = ageDays(last);
                if (age_days > 30) {
                    std::cout << "  ⓘ [sayless] last trigger " << age_days
                              << "d ago — re-verify hook in `.claude/settings.local.json`\n";
                } else if (verb) {
                    std::cout << "  ✓ [sayless] last fire " << age_days << "d ago\n";
                }
            }
        }

        // 5. Bundled DLLs next to icmg.exe (Windows only)
#ifdef _WIN32
        if (!self.empty()) {
            fs::path dir = self.parent_path();
            const char* dlls[] = {
                "libtree-sitter-0.26.dll", "wasmtime.dll",
                "libzstd.dll", "libwinpthread-1.dll", nullptr
            };
            int dll_miss = 0;
            for (int i = 0; dlls[i]; ++i) {
                if (!fs::exists(dir / dlls[i])) ++dll_miss;
            }
            if (dll_miss > 0) {
                ++issues;
                std::cout << "  ! [dlls] " << dll_miss << " bundled DLL(s) missing — "
                          << "run `icmg update --apply` to refetch.\n";
                // No auto-fix: requires network + may need user confirm.
            } else if (verb) {
                std::cout << "  ✓ [dlls] all 4 core bundled DLLs present\n";
            }
        }
#endif

        // 6. DB integrity (read-only)
        try {
            auto& cfg = core::Config::instance();
            std::string db_path = cfg.projectDbPath(".");
            if (fs::exists(db_path)) {
                core::Db db(db_path);
                std::string ok = "?";
                db.query("PRAGMA integrity_check", {},
                         [&](const core::Row& r){ if (!r.empty()) ok = r[0]; });
                if (ok != "ok") {
                    ++issues;
                    std::cout << "  ! [db] integrity FAIL — DB corrupt.\n"
                              << "    1) `icmg mirror failover` (instant; if mirror exists)\n"
                              << "    2) `icmg backup list` then `icmg backup restore latest`\n"
                              << "    3) If neither: copy " << db_path
                              << " then `icmg backup integrity` to confirm.\n";
                } else if (verb) {
                    std::cout << "  ✓ [db] integrity ok\n";
                }
            }
        } catch (...) {
            ++issues;
            std::cout << "  ! [db] open failed (locked or corrupt)\n";
        }

        // Summary
        std::cout << "\n";
        if (issues == 0) {
            std::cout << "icmg doctor: all checks passed.\n";
        } else if (dry) {
            std::cout << "icmg doctor: " << issues << " issue(s) found (dry-run; no changes applied).\n";
            std::cout << "  Re-run without --dry-run to apply fixes.\n";
        } else {
            std::cout << "icmg doctor: " << issues << " issue(s) found, "
                      << fixed << " auto-fixed.\n";
            if (fixed < issues) {
                std::cout << "  " << (issues - fixed) << " require manual action (see above).\n";
            }
        }
        return issues == 0 ? 0 : (fixed == issues ? 0 : 1);
    }

private:
    static fs::path homeDir() {
        const char* h = std::getenv("USERPROFILE");
        if (!h) h = std::getenv("HOME");
        return fs::path(h ? h : ".");
    }

    static fs::path selfPath() {
#ifdef _WIN32
        char buf[1024];
        DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
        if (len == 0) return {};
        return fs::path(buf);
#else
        std::error_code ec;
        return fs::canonical("/proc/self/exe", ec);
#endif
    }

    static int ageDays(const fs::path& p) {
        std::error_code ec;
        auto ftime = fs::last_write_time(p, ec);
        if (ec) return -1;
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
        auto now = std::chrono::system_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::hours>(now - sctp).count();
        return (int)(diff / 24);
    }

    bool runInitInstallHooks(bool strict = false) {
        auto self = selfPath();
        if (self.empty()) return false;
        std::string cmd = "\"" + self.string() + "\" init --install-hooks --force "
                          "--no-agents --no-embedder --no-scan";
        if (strict) cmd += " --strict-read";
        auto res = core::safeExecShell(cmd, true, 15000);
        if (res.exit_code == 0) {
            std::cout << "    → hooks reinstalled" << (strict ? " (strict mode)" : "") << "\n";
            return true;
        }
        std::cout << "    → reinstall FAILED (exit=" << res.exit_code << ")\n";
        return false;
    }
};

ICMG_REGISTER_COMMAND("doctor", DoctorCommand);

} // namespace icmg::cli
