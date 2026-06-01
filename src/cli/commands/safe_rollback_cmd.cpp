// `icmg safe-rollback` — safely restore a file to HEAD, protecting uncommitted work.
//
// Solves AI failure mode: AI runs `git checkout <file>` / `git restore` /
// `git reset --hard` without checking disk-vs-HEAD age, wiping uncommitted work.
//
// Steps:
//   1. Verify `git diff --quiet -- <file>` (no uncommitted changes).
//      If dirty: error rc=1 + print diff, unless --force.
//   2. Compute disk mtime vs HEAD commit time. If disk newer → warn (unless --strict).
//   3. Backup current file to ~/.icmg/rollback-backups/<basename>-<unix_ts>.bak.
//   4. Run `git checkout HEAD -- <file>`.
//   5. Print `restored <file> (backup: <backup_path>)`.
//
// Usage:
//   icmg safe-rollback <file> [--force] [--strict] [--no-backup]

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/path_utils.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

class SafeRollbackCommand : public BaseCommand {
public:
    std::string name()        const override { return "safe-rollback"; }
    std::string description() const override {
        return "Safely restore a file to HEAD, backing up uncommitted changes first";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg safe-rollback <file> [options]\n\n"
            "Safely runs `git checkout HEAD -- <file>` with guards:\n"
            "  1. Detects uncommitted changes and shows diff (error unless --force)\n"
            "  2. Warns when disk mtime is newer than HEAD commit (skipped without --strict)\n"
            "  3. Backs up current file to ~/.icmg/rollback-backups/ before checkout\n\n"
            "Options:\n"
            "  --force       Proceed even when file has uncommitted changes\n"
            "  --strict      Treat disk-newer-than-HEAD as an error\n"
            "  --no-backup   Skip backup step\n"
            "  --help, -h    Show this help\n\n"
            "Examples:\n"
            "  icmg safe-rollback src/main.cpp\n"
            "  icmg safe-rollback src/main.cpp --force --no-backup\n\n"
            "Note: To test dirty-file detection interactively, modify a tracked file\n"
            "and run `icmg safe-rollback <file>` — it will show the diff and abort.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage();
            return 0;
        }
        if (args.empty()) {
            std::cerr << "icmg safe-rollback: missing <file> argument\n";
            usage();
            return 1;
        }

        // First positional arg is the file path.
        std::string file;
        for (const auto& a : args) {
            if (!a.empty() && a[0] != '-') { file = a; break; }
        }

        if (file.empty()) {
            std::cerr << "icmg safe-rollback: <file> argument required\n";
            usage();
            return 1;
        }

        bool force     = hasFlag(args, "--force");
        bool strict    = hasFlag(args, "--strict");
        bool no_backup = hasFlag(args, "--no-backup");

        // Verify file exists on disk.
        if (!fs::exists(file)) {
            std::cerr << "icmg safe-rollback: file not found: " << file << "\n";
            return 1;
        }

        // --- Step 1: Detect uncommitted changes --------------------------------
        auto diff_result = core::safeExecShell(
            "git diff --quiet -- " + shellQuote(file),
            /*merge_stderr=*/true,
            /*timeout_ms=*/10000);

        bool is_dirty = (diff_result.exit_code != 0);

        if (is_dirty) {
            // Show the diff to the user.
            auto diff_show = core::safeExecShell(
                "git diff -- " + shellQuote(file),
                /*merge_stderr=*/true,
                /*timeout_ms=*/10000);
            if (!diff_show.out.empty()) {
                std::cerr << "icmg safe-rollback: uncommitted changes in " << file << ":\n";
                std::cerr << diff_show.out << "\n";
            } else {
                std::cerr << "icmg safe-rollback: file has uncommitted changes: " << file << "\n";
            }

            if (!force) {
                std::cerr << "Aborting. Use --force to proceed anyway.\n";
                return 1;
            }
            std::cerr << "--force: proceeding despite uncommitted changes.\n";
        }

        // --- Step 2: mtime vs HEAD commit time check ---------------------------
        std::error_code ec;
        auto mtime = fs::last_write_time(file, ec);
        if (!ec) {
            // Get HEAD commit timestamp for this file.
            auto commit_ts_result = core::safeExecShell(
                "git log -1 --format=%ct -- " + shellQuote(file),
                /*merge_stderr=*/false,
                /*timeout_ms=*/10000);

            if (commit_ts_result.exit_code == 0 && !commit_ts_result.out.empty()) {
                try {
                    int64_t commit_ts = std::stoll(commit_ts_result.out);
                    // Convert file_time_type to unix seconds (C++17-compatible).
                    // file_time_type epoch differs from Unix epoch by a known offset.
                    // Use duration_cast from file_time_type epoch to get a comparable value.
                    auto mtime_dur = mtime.time_since_epoch();
                    // On most C++17 implementations, file_time_type epoch == Unix epoch.
                    // We do a best-effort conversion; mtime comparison is advisory only.
                    int64_t disk_ts = std::chrono::duration_cast<std::chrono::seconds>(
                        mtime_dur).count();
                    // Adjust for Windows FILETIME epoch offset if necessary.
                    // FILETIME epoch is Jan 1, 1601; Unix epoch Jan 1, 1970 = 11644473600s offset.
                    // MinGW fs::file_time_type uses a custom epoch; subtract to approximate.
                    // If disk_ts is implausibly large (> year 2100 in Unix time), adjust.
                    static constexpr int64_t kWin32Offset = 11644473600LL;
                    static constexpr int64_t kY2100Unix = 4102444800LL;
                    if (disk_ts > kY2100Unix) disk_ts -= kWin32Offset;

                    if (disk_ts > commit_ts) {
                        std::string warn_msg =
                            "icmg safe-rollback: WARNING: disk mtime ("
                            + std::to_string(disk_ts)
                            + ") is newer than HEAD commit time ("
                            + std::to_string(commit_ts)
                            + ") — file may have unsaved work.\n";
                        std::cerr << warn_msg;

                        if (strict) {
                            std::cerr << "--strict: aborting due to disk newer than HEAD.\n";
                            return 1;
                        }
                    }
                } catch (...) {
                    // Non-fatal: can't parse commit timestamp.
                }
            }
        }

        // --- Step 3: Backup current file --------------------------------------
        std::string backup_path;
        if (!no_backup) {
            fs::path backup_dir = fs::path(core::icmgGlobalDir()) / "rollback-backups";
            std::error_code bec;
            fs::create_directories(backup_dir, bec);

            int64_t ts = (int64_t)std::time(nullptr);
            std::string basename = fs::path(file).filename().string();
            backup_path = (backup_dir / (basename + "-" + std::to_string(ts) + ".bak")).string();

            std::error_code cec;
            fs::copy_file(file, backup_path, fs::copy_options::overwrite_existing, cec);
            if (cec) {
                std::cerr << "icmg safe-rollback: WARNING: backup failed: "
                          << cec.message() << " — proceeding without backup.\n";
                backup_path.clear();
            }
        }

        // --- Step 4: git checkout HEAD -- <file> ------------------------------
        auto checkout_result = core::safeExecShell(
            "git checkout HEAD -- " + shellQuote(file),
            /*merge_stderr=*/true,
            /*timeout_ms=*/15000);

        if (checkout_result.exit_code != 0) {
            std::cerr << "icmg safe-rollback: git checkout failed:\n"
                      << checkout_result.out << "\n";
            return 1;
        }

        // --- Step 5: Print success --------------------------------------------
        std::cout << "restored " << file;
        if (!backup_path.empty()) {
            std::cout << " (backup: " << backup_path << ")";
        }
        std::cout << "\n";

        return 0;
    }

private:
    // Shell-quote a file path for use in shell commands.
    // Simple single-quote wrap; handles spaces and most special chars.
    static std::string shellQuote(const std::string& s) {
        // On Windows paths may use backslashes; use double-quotes instead.
#ifdef _WIN32
        return "\"" + s + "\"";
#else
        return "'" + s + "'";
#endif
    }
};

ICMG_REGISTER_COMMAND("safe-rollback", SafeRollbackCommand);

} // namespace icmg::cli
