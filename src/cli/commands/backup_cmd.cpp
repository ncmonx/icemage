// Phase 74 T1: `icmg backup` — self-protection against DB corruption.
//
// Subcommands:
//   snapshot [--note STR]                         atomic point-in-time copy
//   list [--json]                                 enumerate snapshots
//   restore <id|latest> [--force]                 atomic restore w/ safety net
//   verify [<id>]                                 sha256 check sidecars
//   prune [--keep-hourly N --keep-daily N --keep-weekly N --keep-monthly N]
//                                                 pyramidal retention
//   integrity                                     PRAGMA integrity_check on current
//
// Storage: <project>/.icmg/backups/YYYYMMDD-HHMMSS.db + .sha256 + .note (optional)
//
// Safety: snapshot uses sqlite3_backup_* API (handles open WAL), atomic.
// Restore creates pre-restore snapshot first ("undo"), then swaps via fs::rename.
// Schema-version mismatch → restore refuses unless --force.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/audit_log.hpp"
#include "../../core/repair_counter.hpp"

#include <sqlite3.h>
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

namespace fs = std::filesystem;

namespace icmg::cli {

class BackupCommand : public BaseCommand {
public:
    std::string name()        const override { return "backup"; }
    std::string description() const override {
        return "Snapshot/restore/verify/prune project DB (anti-corruption)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg backup <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  snapshot [--note STR]    Atomic point-in-time DB copy + sha256\n"
            "  list [--json]            Show all snapshots (size, age, note)\n"
            "  restore <id|latest>      Restore from snapshot (creates undo first)\n"
            "  verify [<id>]            Recompute sha256 of snapshot(s)\n"
            "  prune [--keep-hourly 24 --keep-daily 7 --keep-weekly 4 --keep-monthly 6]\n"
            "                           Pyramidal retention (default 24h/7d/4w/6m)\n"
            "  integrity                PRAGMA integrity_check on live DB\n"
            "  auto-on  [--interval Nh] Schedule periodic snapshot+prune (default 1h)\n"
            "  auto-off                 Remove scheduled task for this project\n"
            "  auto-status              Show schedule install + last run\n\n"
            "Storage: <project>/.icmg/backups/<UTC-stamp>.db (+ .sha256, .note)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }

        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        if (sub == "snapshot")  return cmdSnapshot(rest);
        if (sub == "list")      return cmdList(rest);
        if (sub == "restore")   return cmdRestore(rest);
        if (sub == "verify")    return cmdVerify(rest);
        if (sub == "prune")     return cmdPrune(rest);
        if (sub == "integrity") return cmdIntegrity(rest);
        if (sub == "auto-on")     return cmdAutoOn(rest);
        if (sub == "auto-off")    return cmdAutoOff(rest);
        if (sub == "auto-status") return cmdAutoStatus(rest);

        std::cerr << "icmg backup: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    // ---- helpers ---------------------------------------------------------

    static fs::path projectRoot() { return fs::current_path(); }
    static fs::path dbPath()      { return projectRoot() / ".icmg" / "data.db"; }
    static fs::path backupDir()   { return projectRoot() / ".icmg" / "backups"; }

    static std::string utcStamp() {
        auto t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        std::ostringstream o;
        o << std::put_time(&tm, "%Y%m%d-%H%M%S");
        return o.str();
    }

    static std::string computeSha256(const fs::path& file) {
#ifdef _WIN32
        std::string cmd = "certutil -hashfile \"" + file.string() + "\" SHA256";
#else
        std::string cmd = "(sha256sum \"" + file.string()
                        + "\" 2>/dev/null || shasum -a 256 \"" + file.string() + "\")";
#endif
        auto res = core::safeExecShell(cmd, false, 30000);
        if (res.exit_code != 0 || res.out.empty()) return {};
        // Find first 64-hex token.
        std::string s = res.out;
        for (size_t i = 0; i + 64 <= s.size(); ++i) {
            bool ok = true;
            for (size_t j = 0; j < 64; ++j) {
                char c = s[i + j];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                    ok = false; break;
                }
            }
            if (ok) {
                std::string hex = s.substr(i, 64);
                std::transform(hex.begin(), hex.end(), hex.begin(), ::tolower);
                return hex;
            }
        }
        return {};
    }

    static std::string humanSize(uintmax_t b) {
        const char* unit[] = {"B", "KB", "MB", "GB"};
        double s = (double)b;
        int u = 0;
        while (s >= 1024.0 && u < 3) { s /= 1024.0; ++u; }
        std::ostringstream o;
        o << std::fixed << std::setprecision(s < 10 ? 2 : 1) << s << unit[u];
        return o.str();
    }

    struct Snap {
        fs::path file;
        std::string id;     // basename without .db
        uintmax_t size = 0;
        std::time_t mtime = 0;
        std::string note;
        std::string sha256;
    };

    static std::vector<Snap> listSnaps() {
        std::vector<Snap> out;
        fs::path dir = backupDir();
        if (!fs::exists(dir)) return out;
        for (auto& e : fs::directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            const auto& p = e.path();
            if (p.extension() != ".db") continue;
            Snap s;
            s.file = p;
            s.id = p.stem().string();
            s.size = fs::file_size(p);
            // file_time → time_t (best effort, portable enough)
            auto ftime = fs::last_write_time(p);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
            s.mtime = std::chrono::system_clock::to_time_t(sctp);

            fs::path note_p = p; note_p += ".note";
            if (fs::exists(note_p)) {
                std::ifstream nf(note_p);
                std::getline(nf, s.note);
            }
            fs::path sha_p = p; sha_p += ".sha256";
            if (fs::exists(sha_p)) {
                std::ifstream sf(sha_p);
                sf >> s.sha256;
            }
            out.push_back(std::move(s));
        }
        std::sort(out.begin(), out.end(),
                  [](const Snap& a, const Snap& b){ return a.id > b.id; });
        return out;
    }

    // ---- snapshot --------------------------------------------------------

    int cmdSnapshot(const std::vector<std::string>& args) {
        fs::path src = dbPath();
        if (!fs::exists(src)) {
            std::cerr << "icmg backup: no DB at " << src
                      << " (run `icmg init` first)\n";
            return 1;
        }
        fs::create_directories(backupDir());

        std::string stamp = utcStamp();
        fs::path dst = backupDir() / (stamp + ".db");

        // Atomic backup via SQLite C API.
        sqlite3* src_db = nullptr;
        sqlite3* dst_db = nullptr;
        if (sqlite3_open(src.string().c_str(), &src_db) != SQLITE_OK) {
            std::cerr << "icmg backup: cannot open source DB: "
                      << sqlite3_errmsg(src_db) << "\n";
            if (src_db) sqlite3_close(src_db);
            return 2;
        }
        if (sqlite3_open(dst.string().c_str(), &dst_db) != SQLITE_OK) {
            std::cerr << "icmg backup: cannot create snapshot file: "
                      << sqlite3_errmsg(dst_db) << "\n";
            sqlite3_close(src_db);
            if (dst_db) sqlite3_close(dst_db);
            return 2;
        }
        sqlite3_backup* bk = sqlite3_backup_init(dst_db, "main", src_db, "main");
        if (!bk) {
            std::cerr << "icmg backup: backup_init failed: "
                      << sqlite3_errmsg(dst_db) << "\n";
            sqlite3_close(dst_db); sqlite3_close(src_db);
            return 2;
        }
        int rc = sqlite3_backup_step(bk, -1);  // copy all pages
        sqlite3_backup_finish(bk);
        sqlite3_close(dst_db);
        sqlite3_close(src_db);
        if (rc != SQLITE_DONE) {
            std::cerr << "icmg backup: snapshot incomplete (rc=" << rc << ")\n";
            return 2;
        }

        // Sidecar files.
        std::string sha = computeSha256(dst);
        if (!sha.empty()) {
            fs::path shap = dst; shap += ".sha256";
            std::ofstream f(shap);
            f << sha << "  " << dst.filename().string() << "\n";
        }
        std::string note = flagValue(args, "--note");
        if (!note.empty()) {
            fs::path notep = dst; notep += ".note";
            std::ofstream f(notep);
            f << note << "\n";
        }

        uintmax_t sz = fs::file_size(dst);
        std::cout << "icmg backup: snapshot saved\n"
                  << "  id:    " << stamp << "\n"
                  << "  file:  " << dst.string() << "\n"
                  << "  size:  " << humanSize(sz) << "\n";
        if (!sha.empty()) std::cout << "  sha:   " << sha.substr(0, 16) << "...\n";
        if (!note.empty()) std::cout << "  note:  " << note << "\n";
        return 0;
    }

    // ---- list ------------------------------------------------------------

    int cmdList(const std::vector<std::string>& args) {
        auto snaps = listSnaps();
        bool json = hasFlag(args, "--json");
        if (json) {
            std::cout << "[";
            bool first = true;
            for (auto& s : snaps) {
                if (!first) std::cout << ",";
                first = false;
                std::cout << "{\"id\":\"" << s.id << "\""
                          << ",\"size\":" << s.size
                          << ",\"mtime\":" << s.mtime
                          << ",\"note\":\"" << s.note << "\""
                          << ",\"sha256\":\"" << s.sha256 << "\"}";
            }
            std::cout << "]\n";
            return 0;
        }
        if (snaps.empty()) {
            std::cout << "No snapshots. Run `icmg backup snapshot` to create one.\n";
            return 0;
        }
        std::cout << "ID                   SIZE      AGE      NOTE\n";
        std::cout << "-----------------------------------------------------\n";
        std::time_t now = std::time(nullptr);
        for (auto& s : snaps) {
            long age_min = (now - s.mtime) / 60;
            std::string age;
            if      (age_min < 60)        age = std::to_string(age_min) + "m";
            else if (age_min < 60 * 24)   age = std::to_string(age_min / 60) + "h";
            else                          age = std::to_string(age_min / 1440) + "d";
            std::cout << s.id << "  "
                      << std::left << std::setw(8) << humanSize(s.size) << "  "
                      << std::left << std::setw(7) << age << "  "
                      << s.note << "\n";
        }
        std::cout << "Total: " << snaps.size() << " snapshot(s)\n";
        return 0;
    }

    // ---- restore ---------------------------------------------------------

    int cmdRestore(const std::vector<std::string>& args) {
        if (args.empty() || args[0][0] == '-') {
            std::cerr << "icmg backup restore: missing <id|latest>\n";
            return 1;
        }
        std::string target = args[0];
        bool force = hasFlag(args, "--force");
        // Phase 75: loop guard.
        {
            core::RepairCounter rc;
            if (!rc.tryRepair("backup-restore", 3)) {
                std::cerr << "icmg backup restore: HALTED — >3 restores in last hour.\n"
                          << "  Investigate root cause before retrying. Reset:\n"
                          << "    rm ~/.icmg/repair-counter.json\n";
                return 4;
            }
        }

        auto snaps = listSnaps();
        if (snaps.empty()) {
            std::cerr << "icmg backup restore: no snapshots\n";
            return 1;
        }
        Snap chosen{};
        if (target == "latest") {
            chosen = snaps.front();
        } else {
            for (auto& s : snaps) if (s.id == target) { chosen = s; break; }
            if (chosen.id.empty()) {
                std::cerr << "icmg backup restore: snapshot '" << target << "' not found\n";
                return 1;
            }
        }

        fs::path live = dbPath();
        bool live_exists = fs::exists(live);

        // Schema-version safety check.
        if (live_exists && !force) {
            try {
                core::Db live_db(live.string());
                int live_v = live_db.userVersion();
                core::Db snap_db(chosen.file.string());
                int snap_v = snap_db.userVersion();
                if (live_v != snap_v) {
                    std::cerr << "icmg backup restore: schema mismatch (live="
                              << live_v << " snap=" << snap_v
                              << "); use --force to override\n";
                    return 2;
                }
            } catch (const std::exception& e) {
                std::cerr << "icmg backup restore: schema check failed: " << e.what()
                          << "; use --force to override\n";
                return 2;
            }
        }

        // Pre-restore snapshot ("undo").
        if (live_exists) {
            std::cout << "Creating undo snapshot of current DB...\n";
            std::vector<std::string> undo_args = {"--note", "auto-undo before restore " + chosen.id};
            (void)cmdSnapshot(undo_args);
        }

        // Atomic swap: copy snapshot → temp in same dir, then rename over live.
        // Avoid cross-device move; same fs guaranteed (both inside .icmg/).
        fs::path tmp = live;
        tmp += ".restore-tmp";
        try {
            fs::copy_file(chosen.file, tmp, fs::copy_options::overwrite_existing);
            // Drop WAL/SHM so SQLite re-initializes cleanly on next open.
            fs::path wal = live; wal += "-wal";
            fs::path shm = live; shm += "-shm";
            fs::remove(wal);
            fs::remove(shm);
            fs::rename(tmp, live);
        } catch (const std::exception& e) {
            std::cerr << "icmg backup restore: copy failed: " << e.what() << "\n";
            fs::remove(tmp);
            return 2;
        }

        std::cout << "icmg backup: restored from " << chosen.id << "\n"
                  << "  bytes: " << humanSize(chosen.size) << "\n"
                  << "  Run `icmg health` to confirm integrity.\n";
        // Phase 75: audit log.
        try {
            core::AuditLog al((projectRoot() / ".icmg" / "audit.log").string());
            al.append("backup", "RESTORE",
                      "from=" + chosen.id + " size=" + std::to_string(chosen.size));
        } catch (...) {}
        return 0;
    }

    // ---- verify ----------------------------------------------------------

    int cmdVerify(const std::vector<std::string>& args) {
        auto snaps = listSnaps();
        std::string only = args.empty() ? "" : (args[0][0] == '-' ? "" : args[0]);
        int ok_count = 0, bad_count = 0, missing = 0;
        for (auto& s : snaps) {
            if (!only.empty() && s.id != only) continue;
            if (s.sha256.empty()) {
                std::cout << "[?]  " << s.id << "  (no sidecar)\n";
                ++missing; continue;
            }
            std::string actual = computeSha256(s.file);
            if (actual.empty()) {
                std::cout << "[?]  " << s.id << "  (cannot compute)\n";
                ++missing; continue;
            }
            if (actual == s.sha256) {
                std::cout << "[OK] " << s.id << "  " << actual.substr(0, 16) << "...\n";
                ++ok_count;
            } else {
                std::cout << "[!!]  " << s.id << "  MISMATCH\n"
                          << "      expected " << s.sha256 << "\n"
                          << "      actual   " << actual << "\n";
                ++bad_count;
            }
        }
        std::cout << "Verify: " << ok_count << " ok, "
                  << bad_count << " bad, " << missing << " missing\n";
        return bad_count > 0 ? 2 : 0;
    }

    // ---- prune (pyramidal retention) -------------------------------------

    int cmdPrune(const std::vector<std::string>& args) {
        int keep_h = 24, keep_d = 7, keep_w = 4, keep_m = 6;
        try {
            std::string v;
            if (!(v = flagValue(args, "--keep-hourly")).empty())  keep_h = std::stoi(v);
            if (!(v = flagValue(args, "--keep-daily")).empty())   keep_d = std::stoi(v);
            if (!(v = flagValue(args, "--keep-weekly")).empty())  keep_w = std::stoi(v);
            if (!(v = flagValue(args, "--keep-monthly")).empty()) keep_m = std::stoi(v);
        } catch (...) {}
        bool dry = hasFlag(args, "--dry-run");

        auto snaps = listSnaps();   // newest first
        std::time_t now = std::time(nullptr);

        // Buckets: each snapshot assigned to (hour|day|week|month) tier.
        // Keep newest in each unique (tier, bucket-key); drop rest.
        struct Tier { const char* name; int keep; long secs_per_bucket; };
        Tier tiers[] = {
            {"hourly",  keep_h, 3600},
            {"daily",   keep_d, 86400},
            {"weekly",  keep_w, 86400 * 7},
            {"monthly", keep_m, 86400 * 30},
        };

        std::vector<bool> keep(snaps.size(), false);
        for (auto& t : tiers) {
            std::vector<long> seen_buckets;
            int kept = 0;
            for (size_t i = 0; i < snaps.size() && kept < t.keep; ++i) {
                long bucket = (now - snaps[i].mtime) / t.secs_per_bucket;
                if (std::find(seen_buckets.begin(), seen_buckets.end(), bucket) != seen_buckets.end())
                    continue;
                seen_buckets.push_back(bucket);
                keep[i] = true;
                ++kept;
            }
        }

        int kept = 0, deleted = 0;
        uintmax_t freed = 0;
        for (size_t i = 0; i < snaps.size(); ++i) {
            if (keep[i]) { ++kept; continue; }
            ++deleted;
            freed += snaps[i].size;
            std::cout << (dry ? "[would-rm] " : "[rm] ") << snaps[i].id << "\n";
            if (!dry) {
                fs::remove(snaps[i].file);
                fs::path shap = snaps[i].file; shap += ".sha256"; fs::remove(shap);
                fs::path notep = snaps[i].file; notep += ".note";  fs::remove(notep);
            }
        }
        std::cout << "Prune " << (dry ? "(dry-run)" : "applied")
                  << ": kept " << kept << ", removed " << deleted
                  << ", freed " << humanSize(freed) << "\n";
        return 0;
    }

    // ---- integrity -------------------------------------------------------

    // ---- auto-on/off/status (scheduled task per project) ---------------

    // Stable per-project task name. Uses last 8 chars of root path hash
    // (FNV-1a 32-bit) so concurrent projects don't collide.
    static std::string taskName() {
        std::string s = projectRoot().string();
        uint32_t h = 2166136261u;
        for (char c : s) { h ^= (uint8_t)c; h *= 16777619u; }
        std::ostringstream o;
        o << "icmg-backup-" << std::hex << std::setw(8) << std::setfill('0') << h;
        return o.str();
    }

    // Cmd executed by scheduler. Snapshot + prune; absolute paths.
    static std::string scheduledCommand() {
        std::string root = projectRoot().string();
        // Replace backslashes for cmd.exe quoting safety on Windows.
        std::string esc = root;
#ifdef _WIN32
        std::replace(esc.begin(), esc.end(), '\\', '/');
#endif
        return "cd \"" + esc + "\" && icmg backup snapshot --note auto-hourly && "
               "icmg backup prune";
    }

    int cmdAutoOn(const std::vector<std::string>& args) {
        std::string interval = flagValue(args, "--interval", "1h");
        // Parse: NUM(h|m). Default 60min.
        int minutes = 60;
        if (!interval.empty()) {
            char unit = interval.back();
            try {
                int n = std::stoi(interval.substr(0, interval.size() - 1));
                if      (unit == 'h') minutes = n * 60;
                else if (unit == 'm') minutes = n;
                else                  minutes = std::stoi(interval);  // raw int = minutes
            } catch (...) {}
        }
        if (minutes < 5) {
            std::cerr << "icmg backup auto-on: interval too small (min 5m)\n";
            return 1;
        }
        std::string tn = taskName();
        std::string cmd = scheduledCommand();
#ifdef _WIN32
        // schtasks: /SC MINUTE /MO N for minute interval; /SC HOURLY /MO N for hours.
        std::string sched;
        if (minutes >= 60 && (minutes % 60) == 0)
            sched = "/SC HOURLY /MO " + std::to_string(minutes / 60);
        else
            sched = "/SC MINUTE /MO " + std::to_string(minutes);
        std::string full = "schtasks /Create " + sched + " /TN \"" + tn
                         + "\" /TR \"bash -lc '" + cmd + "'\" /F";
        auto res = core::safeExecShell(full, true, 15000);
        if (res.exit_code != 0) {
            std::cerr << "icmg backup auto-on: schtasks failed: " << res.err << "\n";
            return 2;
        }
        std::cout << "icmg backup auto-on: installed task '" << tn << "'\n"
                  << "  interval: every " << minutes << " min\n"
                  << "  command:  " << cmd << "\n"
                  << "  Verify:   schtasks /Query /TN " << tn << "\n";
#else
        // crontab: */N for minutes < 60; 0 */H for hours.
        std::string cron_expr;
        if (minutes < 60)        cron_expr = "*/" + std::to_string(minutes) + " * * * *";
        else if ((minutes % 60) == 0)
                                 cron_expr = "0 */" + std::to_string(minutes / 60) + " * * *";
        else                     cron_expr = "*/" + std::to_string(minutes) + " * * * *";
        std::string entry = cron_expr + "  " + cmd + "  # " + tn + "\n";

        auto cur = core::safeExecShell("crontab -l 2>/dev/null", false, 5000);
        std::string tab = cur.exit_code == 0 ? cur.out : "";
        // Strip existing entry for this project (idempotent re-install).
        std::ostringstream filtered;
        std::istringstream is(tab);
        std::string line;
        while (std::getline(is, line))
            if (line.find("# " + tn) == std::string::npos) filtered << line << "\n";
        std::string newtab = filtered.str() + entry;
        std::string tmp = "/tmp/icmg-backup-cron.tmp";
        std::ofstream f(tmp); f << newtab; f.close();
        auto res = core::safeExecShell("crontab " + tmp, true, 5000);
        std::remove(tmp.c_str());
        if (res.exit_code != 0) {
            std::cerr << "icmg backup auto-on: crontab failed: " << res.err << "\n";
            return 2;
        }
        std::cout << "icmg backup auto-on: installed crontab entry\n"
                  << "  cron: " << cron_expr << "\n"
                  << "  Verify: crontab -l | grep " << tn << "\n";
#endif
        // Marker for status cmd.
        fs::create_directories(projectRoot() / ".icmg");
        std::ofstream m(projectRoot() / ".icmg" / "backup-auto.flag");
        m << minutes << "\n";
        return 0;
    }

    int cmdAutoOff(const std::vector<std::string>&) {
        std::string tn = taskName();
#ifdef _WIN32
        std::string cmd = "schtasks /Delete /TN \"" + tn + "\" /F";
        auto res = core::safeExecShell(cmd, true, 5000);
        if (res.exit_code != 0) {
            std::cerr << "icmg backup auto-off: not installed or failed\n";
            // continue to clear flag anyway
        } else {
            std::cout << "icmg backup auto-off: removed task '" << tn << "'\n";
        }
#else
        auto cur = core::safeExecShell("crontab -l 2>/dev/null", false, 5000);
        if (cur.exit_code == 0 && !cur.out.empty()) {
            std::ostringstream filtered;
            std::istringstream is(cur.out);
            std::string line;
            bool found = false;
            while (std::getline(is, line)) {
                if (line.find("# " + tn) != std::string::npos) { found = true; continue; }
                filtered << line << "\n";
            }
            if (found) {
                std::string tmp = "/tmp/icmg-backup-cron.tmp";
                std::ofstream f(tmp); f << filtered.str(); f.close();
                core::safeExecShell("crontab " + tmp, true, 5000);
                std::remove(tmp.c_str());
                std::cout << "icmg backup auto-off: removed crontab entry\n";
            }
        }
#endif
        fs::remove(projectRoot() / ".icmg" / "backup-auto.flag");
        return 0;
    }

    int cmdAutoStatus(const std::vector<std::string>&) {
        std::string tn = taskName();
        fs::path flag = projectRoot() / ".icmg" / "backup-auto.flag";
        bool flag_set = fs::exists(flag);
        int interval_min = 0;
        if (flag_set) {
            std::ifstream f(flag);
            f >> interval_min;
        }
        std::cout << "icmg backup auto-status (project: "
                  << projectRoot().string() << ")\n"
                  << "  task name: " << tn << "\n"
                  << "  flag:      " << (flag_set ? "ON" : "off") << "\n";
        if (flag_set) std::cout << "  interval:  every " << interval_min << " min\n";

#ifdef _WIN32
        std::string q = "schtasks /Query /TN \"" + tn + "\" /FO LIST 2>nul";
        auto res = core::safeExecShell(q, false, 5000);
        if (res.exit_code == 0 && !res.out.empty()) {
            std::cout << "  installed: YES\n";
            // Pull "Last Run Time" line if present.
            size_t p = res.out.find("Last Run Time:");
            if (p != std::string::npos) {
                size_t e = res.out.find('\n', p);
                std::cout << "  " << res.out.substr(p, e - p) << "\n";
            }
        } else {
            std::cout << "  installed: no\n";
        }
#else
        auto cur = core::safeExecShell("crontab -l 2>/dev/null | grep " + tn, false, 5000);
        std::cout << "  installed: " << (cur.exit_code == 0 && !cur.out.empty() ? "YES" : "no")
                  << "\n";
#endif
        // Latest snapshot from listSnaps.
        auto snaps = listSnaps();
        if (!snaps.empty()) {
            std::time_t now = std::time(nullptr);
            long age_min = (now - snaps.front().mtime) / 60;
            std::cout << "  latest snapshot: " << snaps.front().id
                      << " (" << age_min << "m ago)\n";
        } else {
            std::cout << "  latest snapshot: (none)\n";
        }
        return 0;
    }

    int cmdIntegrity(const std::vector<std::string>&) {
        fs::path live = dbPath();
        if (!fs::exists(live)) {
            std::cerr << "icmg backup integrity: no DB at " << live << "\n";
            return 1;
        }
        try {
            core::Db db(live.string());
            std::string result;
            db.query("PRAGMA integrity_check", {},
                     [&](const core::Row& r){ if (!r.empty()) result = r[0]; });
            std::cout << "integrity_check: " << result << "\n";
            return result == "ok" ? 0 : 2;
        } catch (const std::exception& e) {
            std::cerr << "icmg backup integrity: " << e.what() << "\n";
            return 2;
        }
    }
};

ICMG_REGISTER_COMMAND("backup", BackupCommand);

} // namespace icmg::cli
