// Phase 74 T7: `icmg mirror` — ping-pong dual-mirror failover.
//
// Two-file mirror set (mirror-a / mirror-b) provides hot failover when
// primary data.db corrupts. Distinct from `icmg backup` (history archive):
//   - backup = N-snapshot history, recovery point objective minutes-to-hours
//   - mirror = 2-file ping-pong, recovery point objective seconds, swap-in
//
// Disk cost: exactly 2× live DB size (predictable, bounded).
//
// Subcommands:
//   sync                  Refresh oldest mirror (atomic copy via _backup API)
//   status                Show mirror ages + integrity flag per file
//   failover              If primary fails integrity_check, atomic-swap newer mirror
//   verify                integrity_check on each mirror
//   auto-on [--every Nm]  Schedule periodic sync (default 15m)
//   auto-off
//   auto-status
//
// Sync algorithm:
//   1. mtime-compare mirror-a vs mirror-b → pick OLDER as write target
//   2. open primary read, open target write, sqlite3_backup_step(-1)
//   3. write target.sha256 sidecar
//   4. atomic by SQLite contract; partial copy never replaces target

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/audit_log.hpp"
#include "../../core/repair_counter.hpp"
#include "../../core/schedule_helper.hpp"

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

class MirrorCommand : public BaseCommand {
public:
    std::string name()        const override { return "mirror"; }
    std::string description() const override {
        return "Dual-mirror failover (sync/status/failover/verify + auto-on)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg mirror <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  sync                       Atomic copy primary → oldest mirror\n"
            "  status                     Mirror ages + integrity flags\n"
            "  failover [--dry-run]       Swap-in newest valid mirror if primary corrupt\n"
            "  verify                     PRAGMA integrity_check on each mirror\n"
            "  auto-on [--every Nm]       Schedule periodic sync (default 15m)\n"
            "  auto-off\n"
            "  auto-status\n\n"
            "Disk cost: ~2× live DB size (data.db.mirror-a + data.db.mirror-b).\n"
            "For history retention, use `icmg backup` (separate dir, pyramidal).\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        if (sub == "sync")        return cmdSync(rest);
        if (sub == "status")      return cmdStatus(rest);
        if (sub == "failover")    return cmdFailover(rest);
        if (sub == "verify")      return cmdVerify(rest);
        if (sub == "auto-on")     return cmdAutoOn(rest);
        if (sub == "auto-off")    return cmdAutoOff(rest);
        if (sub == "auto-status") return cmdAutoStatus(rest);
        std::cerr << "icmg mirror: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    static fs::path projectRoot() { return fs::current_path(); }
    static fs::path dbPath()      { return projectRoot() / ".icmg" / "data.db"; }
    static fs::path mirrorA()     { return projectRoot() / ".icmg" / "data.db.mirror-a"; }
    static fs::path mirrorB()     { return projectRoot() / ".icmg" / "data.db.mirror-b"; }

    static std::time_t mtime(const fs::path& p) {
        std::error_code ec;
        if (!fs::exists(p, ec)) return 0;
        auto ftime = fs::last_write_time(p, ec);
        if (ec) return 0;
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
        return std::chrono::system_clock::to_time_t(sctp);
    }

    // Atomic copy via SQLite backup API (handles open WAL).
    static bool copyDb(const fs::path& src, const fs::path& dst, std::string& err) {
        sqlite3* sdb = nullptr;
        sqlite3* ddb = nullptr;
        if (sqlite3_open(src.string().c_str(), &sdb) != SQLITE_OK) {
            err = sdb ? sqlite3_errmsg(sdb) : "open src";
            if (sdb) sqlite3_close(sdb);
            return false;
        }
        if (sqlite3_open(dst.string().c_str(), &ddb) != SQLITE_OK) {
            err = ddb ? sqlite3_errmsg(ddb) : "open dst";
            sqlite3_close(sdb);
            if (ddb) sqlite3_close(ddb);
            return false;
        }
        sqlite3_backup* bk = sqlite3_backup_init(ddb, "main", sdb, "main");
        if (!bk) { err = sqlite3_errmsg(ddb); sqlite3_close(ddb); sqlite3_close(sdb); return false; }
        int rc = sqlite3_backup_step(bk, -1);
        sqlite3_backup_finish(bk);
        sqlite3_close(ddb);
        sqlite3_close(sdb);
        if (rc != SQLITE_DONE) { err = "backup_step rc=" + std::to_string(rc); return false; }
        return true;
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

    static bool integrityOk(const fs::path& p) {
        if (!fs::exists(p)) return false;
        try {
            core::Db d(p.string());
            std::string ok = "?";
            d.query("PRAGMA integrity_check", {},
                    [&](const core::Row& r){ if (!r.empty()) ok = r[0]; });
            return ok == "ok";
        } catch (...) { return false; }
    }

    // ---- sync ------------------------------------------------------------

    int cmdSync(const std::vector<std::string>&) {
        if (!fs::exists(dbPath())) {
            std::cerr << "icmg mirror sync: no DB at " << dbPath() << "\n";
            return 1;
        }
        // Pick older mirror as write target (so newer is preserved on partial fail).
        std::time_t ta = mtime(mirrorA());
        std::time_t tb = mtime(mirrorB());
        fs::path target = (ta == 0 || (tb != 0 && ta < tb)) ? mirrorA() : mirrorB();
        // If neither exists, write to mirror-a first.
        if (ta == 0 && tb == 0) target = mirrorA();

        std::string err;
        if (!copyDb(dbPath(), target, err)) {
            std::cerr << "icmg mirror sync: copy failed: " << err << "\n";
            return 2;
        }
        std::string sha = computeSha256(target);
        if (!sha.empty()) {
            fs::path s = target; s += ".sha256";
            std::ofstream f(s); f << sha << "  " << target.filename().string() << "\n";
        }
        std::cout << "icmg mirror sync: refreshed " << target.filename().string()
                  << " (" << (fs::file_size(target) / 1024 / 1024) << " MB)\n";
        return 0;
    }

    // ---- status ----------------------------------------------------------

    int cmdStatus(const std::vector<std::string>&) {
        std::time_t now = std::time(nullptr);
        auto row = [&](const std::string& nm, const fs::path& p){
            std::cout << "  " << std::left << std::setw(28) << nm;
            if (!fs::exists(p)) { std::cout << "(missing)\n"; return; }
            std::time_t t = mtime(p);
            long age_min = t ? (now - t) / 60 : 0;
            std::cout << std::right << std::setw(6)
                      << (fs::file_size(p) / 1024 / 1024) << " MB  "
                      << std::setw(5) << age_min << "m old"
                      << "  integrity=" << (integrityOk(p) ? "OK" : "BAD") << "\n";
        };
        std::cout << "icmg mirror status\n" << std::string(60, '-') << "\n";
        row("primary  data.db",        dbPath());
        row("mirror-a data.db.mirror-a", mirrorA());
        row("mirror-b data.db.mirror-b", mirrorB());
        return 0;
    }

    // ---- failover --------------------------------------------------------

    int cmdFailover(const std::vector<std::string>& args) {
        bool dry = hasFlag(args, "--dry-run");
        if (integrityOk(dbPath())) {
            std::cout << "icmg mirror failover: primary OK; no swap needed.\n";
            return 0;
        }
        // Phase 75: loop guard. If failover fired >3× in last hour, halt.
        if (!dry) {
            core::RepairCounter rc;
            if (!rc.tryRepair("mirror-failover", 3)) {
                std::cerr << "icmg mirror failover: HALTED — >3 failovers in last hour.\n"
                          << "  Likely persistent corruption. Investigate manually:\n"
                          << "    icmg backup integrity\n"
                          << "    icmg backup list / restore\n"
                          << "    icmg repair-history\n"
                          << "  Reset guard: rm ~/.icmg/repair-counter.json\n";
                return 4;
            }
        }
        std::cerr << "icmg mirror failover: primary integrity BAD — searching mirrors...\n";
        // Pick newest valid mirror.
        std::vector<std::pair<fs::path, std::time_t>> cands;
        if (fs::exists(mirrorA())) cands.push_back({mirrorA(), mtime(mirrorA())});
        if (fs::exists(mirrorB())) cands.push_back({mirrorB(), mtime(mirrorB())});
        std::sort(cands.begin(), cands.end(),
                  [](auto& a, auto& b){ return a.second > b.second; });
        for (auto& [p, t] : cands) {
            if (!integrityOk(p)) {
                std::cerr << "  " << p.filename().string() << " BAD; skipping\n";
                continue;
            }
            std::cout << "  Selected " << p.filename().string()
                      << " (" << ((std::time(nullptr) - t) / 60) << "m old)\n";
            if (dry) {
                std::cout << "  [dry-run] would swap-in.\n";
                return 0;
            }
            // Move primary aside (do not delete; user may want forensics).
            fs::path quarantine = dbPath();
            quarantine += ".CORRUPT-" + std::to_string(std::time(nullptr));
            std::error_code ec;
            fs::rename(dbPath(), quarantine, ec);
            // Drop WAL/SHM siblings (stale).
            fs::remove(fs::path(dbPath().string() + "-wal"));
            fs::remove(fs::path(dbPath().string() + "-shm"));
            // Copy mirror → primary.
            fs::copy_file(p, dbPath(), fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::cerr << "  copy failed: " << ec.message() << "\n";
                return 2;
            }
            std::cout << "icmg mirror failover: swapped in " << p.filename().string()
                      << "\n  Corrupt primary preserved at " << quarantine.filename().string()
                      << "\n  Run `icmg health` to confirm.\n";
            // Phase 75: HMAC-chained audit log.
            try {
                core::AuditLog al((projectRoot() / ".icmg" / "audit.log").string());
                std::string payload =
                    "from=" + p.filename().string()
                    + " quarantined=" + quarantine.filename().string()
                    + " size=" + std::to_string(fs::file_size(dbPath()));
                al.append("mirror", "FAILOVER", payload);
            } catch (...) {}
            // Post-repair self-test: re-verify primary integrity.
            if (!integrityOk(dbPath())) {
                std::cerr << "icmg mirror failover: post-swap integrity FAILED — "
                          << "swapped-in mirror also corrupt. Try `icmg backup restore latest`.\n";
                return 5;
            }
            return 0;
        }
        std::cerr << "icmg mirror failover: NO valid mirror found.\n"
                  << "  Try `icmg backup restore latest` instead.\n";
        return 3;
    }

    // ---- verify ----------------------------------------------------------

    int cmdVerify(const std::vector<std::string>&) {
        int bad = 0;
        for (auto& [nm, p] : std::vector<std::pair<std::string, fs::path>>{
                 {"primary", dbPath()}, {"mirror-a", mirrorA()}, {"mirror-b", mirrorB()}}) {
            if (!fs::exists(p)) {
                std::cout << "[?]  " << nm << " (missing)\n";
                continue;
            }
            bool ok = integrityOk(p);
            std::cout << (ok ? "[OK] " : "[!!] ") << nm << "  " << p.string() << "\n";
            if (!ok) ++bad;
        }
        return bad ? 2 : 0;
    }

    // ---- auto schedule ---------------------------------------------------

    static std::string taskName() {
        // path + username → unique per project AND per user on shared servers
        return "icmg-mirror-" + core::icmgTaskHash(projectRoot().string());
    }

    static std::string scheduledCommand() {
        std::string root = projectRoot().string();
        std::string esc = root;
#ifdef _WIN32
        std::replace(esc.begin(), esc.end(), '\\', '/');
#endif
        return "cd \"" + esc + "\" && icmg mirror sync";
    }

    int cmdAutoOn(const std::vector<std::string>& args) {
        std::string interval = flagValue(args, "--every", "15m");
        int minutes = 15;
        if (!interval.empty()) {
            char unit = interval.back();
            try {
                int n = std::stoi(interval.substr(0, interval.size() - 1));
                if      (unit == 'h') minutes = n * 60;
                else if (unit == 'm') minutes = n;
                else                  minutes = std::stoi(interval);
            } catch (...) {}
        }
        if (minutes < 5) {
            std::cerr << "icmg mirror auto-on: interval too small (min 5m)\n";
            return 1;
        }
        std::string tn = taskName();
#ifdef _WIN32
        // Phase 78: bulletproof scheduler via core::registerWindowsSchedule.
        fs::path wrapper = projectRoot() / ".icmg" / "sched" / (tn + ".cmd");
        fs::create_directories(wrapper.parent_path());
        std::ofstream wf(wrapper, std::ios::binary);
        wf << "@echo off\r\n"
           << "cd /d \"" << projectRoot().string() << "\"\r\n"
           << "echo === %DATE% %TIME% mirror ===>> .icmg\\sched\\sched.log\r\n"
           << "icmg mirror sync >> .icmg\\sched\\sched.log 2>&1\r\n";
        wf.close();
        core::ScheduleSpec spec{tn, wrapper.string(), minutes, "mirror"};
        int rc = core::registerWindowsSchedule(spec);
        if (rc != 0) return rc;
#else
        std::string cron_expr = (minutes < 60)
            ? "*/" + std::to_string(minutes) + " * * * *"
            : "0 */" + std::to_string(minutes / 60) + " * * *";
        std::string entry = cron_expr + "  " + cmd + "  # " + tn + "\n";
        auto cur = core::safeExecShell("crontab -l 2>/dev/null", false, 5000);
        std::string tab = cur.exit_code == 0 ? cur.out : "";
        std::ostringstream filtered;
        std::istringstream is(tab);
        std::string line;
        while (std::getline(is, line))
            if (line.find("# " + tn) == std::string::npos) filtered << line << "\n";
        std::string newtab = filtered.str() + entry;
        std::string tmp = "/tmp/icmg-mirror-cron.tmp";
        std::ofstream f(tmp); f << newtab; f.close();
        auto res = core::safeExecShell("crontab " + tmp, true, 5000);
        std::remove(tmp.c_str());
        if (res.exit_code != 0) { std::cerr << "icmg mirror auto-on: crontab failed\n"; return 2; }
        std::cout << "icmg mirror auto-on: cron " << cron_expr << "\n";
#endif
        std::ofstream m(projectRoot() / ".icmg" / "mirror-auto.flag");
        m << minutes << "\n";
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
            std::string tmp = "/tmp/icmg-mirror-cron.tmp";
            std::ofstream f(tmp); f << filtered.str(); f.close();
            core::safeExecShell("crontab " + tmp, true, 5000);
            std::remove(tmp.c_str());
        }
#endif
        fs::remove(projectRoot() / ".icmg" / "mirror-auto.flag");
        std::cout << "icmg mirror auto-off: cleared\n";
        return 0;
    }

    int cmdAutoStatus(const std::vector<std::string>&) {
        fs::path flag = projectRoot() / ".icmg" / "mirror-auto.flag";
        bool on = fs::exists(flag);
        int min_v = 0;
        if (on) { std::ifstream f(flag); f >> min_v; }
        std::cout << "icmg mirror auto-status\n"
                  << "  task: " << taskName() << "\n"
                  << "  flag: " << (on ? "ON" : "off") << "\n";
        if (on) std::cout << "  every: " << min_v << "m\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("mirror", MirrorCommand);

} // namespace icmg::cli
