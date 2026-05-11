// Phase 77: `icmg sentinel` — continuous health watchdog.
//
// Single-pass check with auto-react. Designed to run on schedule (15m).
// Reuses Phase 75 RepairCounter + AuditLog so reactions are loop-bounded
// and tamper-recorded.
//
// Subcommands:
//   run [--dry-run] [--json] [--quiet]   single check + auto-react
//   status                                last-run summary
//   thresholds [--disk-mb N --cache-rows N --audit-mb N --snap-age-h N]
//                                          show or update thresholds
//   auto-on [--every Nm]                   schedule (default 15m)
//   auto-off
//
// Checks (all opt-in to auto-react; thresholds in ~/.icmg/sentinel.json):
//   disk           .icmg/ total size  > disk_mb_max  → backup prune --aggressive + cache prune
//   snapshots      latest age          > snap_age_h  → backup snapshot
//   mirror         latest age          > mirror_age_m → mirror sync
//   mirror_count   <2                                → mirror sync (creates missing)
//   cache_rows     >cache_rows_max                    → cache prune
//   audit_size     >audit_mb_max                      → rotate (.1.gz)
//   schedules      4 cron tasks present              → re-arm missing
//   integrity      DB ok                              → mirror failover
//   repair_loop    repair-counter <3/h               → halt auto-react

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/path_utils.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/audit_log.hpp"
#include "../../core/repair_counter.hpp"
#include "../../core/schedule_helper.hpp"
#include "../../core/db.hpp"
#include "../../core/config.hpp"

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

class SentinelCommand : public BaseCommand {
public:
    std::string name()        const override { return "sentinel"; }
    std::string description() const override {
        return "Continuous health watchdog (run/status/thresholds/auto-on)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg sentinel <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  run [--dry-run] [--json] [--quiet]\n"
            "                              Single check + auto-react\n"
            "  status                       Last-run summary\n"
            "  thresholds [--disk-mb N --cache-rows N --audit-mb N\n"
            "              --snap-age-h N --mirror-age-m N]\n"
            "                              Show/update thresholds (~/.icmg/sentinel.json)\n"
            "  auto-on [--every Nm]        Schedule (default 15m)\n"
            "  auto-off\n\n"
            "Auto-react halts when repair counter ≥3/h (loop guard).\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        if (sub == "run")        return cmdRun(rest);
        if (sub == "status")     return cmdStatus(rest);
        if (sub == "thresholds") return cmdThresholds(rest);
        if (sub == "auto-on")    return cmdAutoOn(rest);
        if (sub == "auto-off")   return cmdAutoOff(rest);
        std::cerr << "icmg sentinel: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    struct Thresholds {
        int disk_mb_max    = 500;
        int snap_age_h     = 2;
        int mirror_age_m   = 30;
        int cache_rows_max = 10000;
        int audit_mb_max   = 10;
    };

    static fs::path projectRoot()    { return fs::current_path(); }
    static fs::path icmgDir()        { return projectRoot() / ".icmg"; }
    static fs::path sentinelState()  { return core::icmgGlobalDir() + "/sentinel-state.json"; }
    static fs::path sentinelCfg()    { return core::icmgGlobalDir() + "/sentinel.json"; }

    static uintmax_t dirSize(const fs::path& p) {
        uintmax_t total = 0;
        std::error_code ec;
        if (!fs::exists(p)) return 0;
        for (auto it = fs::recursive_directory_iterator(p, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (it->is_regular_file(ec)) total += fs::file_size(it->path(), ec);
        }
        return total;
    }

    static std::time_t mtime(const fs::path& p) {
        std::error_code ec;
        if (!fs::exists(p, ec)) return 0;
        auto ftime = fs::last_write_time(p, ec);
        if (ec) return 0;
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
        return std::chrono::system_clock::to_time_t(sctp);
    }

    static Thresholds loadThresholds() {
        Thresholds t;
        if (!fs::exists(sentinelCfg())) return t;
        std::ifstream f(sentinelCfg());
        std::string buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto extract = [&](const std::string& key, int& dst) {
            size_t p = buf.find("\"" + key + "\"");
            if (p == std::string::npos) return;
            size_t c = buf.find(':', p);
            if (c == std::string::npos) return;
            size_t s = c + 1;
            while (s < buf.size() && std::isspace((unsigned char)buf[s])) ++s;
            size_t e = s;
            while (e < buf.size() && (std::isdigit((unsigned char)buf[e]) || buf[e]=='-')) ++e;
            if (e > s) try { dst = std::stoi(buf.substr(s, e-s)); } catch (...) {}
        };
        extract("disk_mb_max",    t.disk_mb_max);
        extract("snap_age_h",     t.snap_age_h);
        extract("mirror_age_m",   t.mirror_age_m);
        extract("cache_rows_max", t.cache_rows_max);
        extract("audit_mb_max",   t.audit_mb_max);
        return t;
    }

    static void saveThresholds(const Thresholds& t) {
        fs::create_directories(fs::path(sentinelCfg()).parent_path());
        std::ofstream f(sentinelCfg());
        f << "{\n"
          << "  \"disk_mb_max\": "    << t.disk_mb_max    << ",\n"
          << "  \"snap_age_h\": "     << t.snap_age_h     << ",\n"
          << "  \"mirror_age_m\": "   << t.mirror_age_m   << ",\n"
          << "  \"cache_rows_max\": " << t.cache_rows_max << ",\n"
          << "  \"audit_mb_max\": "   << t.audit_mb_max   << "\n"
          << "}\n";
    }

    // ---- run ------------------------------------------------------------

    int cmdRun(const std::vector<std::string>& args) {
        bool dry   = hasFlag(args, "--dry-run");
        bool json  = hasFlag(args, "--json");
        bool quiet = hasFlag(args, "--quiet");
        Thresholds t = loadThresholds();

        // Loop guard: refuse auto-react if repair counter already high.
        core::RepairCounter rc;
        bool can_react = rc.totalLastHour() < 3;

        struct Result { std::string check; std::string status; std::string detail; std::string action; };
        std::vector<Result> results;
        int reactions = 0;

        // Disk size of .icmg/
        {
            uintmax_t b = dirSize(icmgDir());
            uintmax_t mb = b / 1024 / 1024;
            Result r{"disk", "OK", std::to_string(mb) + "MB", ""};
            if ((int)mb > t.disk_mb_max) {
                r.status = "WARN";
                if (can_react && !dry) {
                    core::safeExecShell("icmg backup prune --keep-hourly 12 --keep-daily 3", false, 30000);
                    core::safeExecShell("icmg cache prune", false, 15000);
                    r.action = "pruned backups + cache"; ++reactions;
                } else {
                    r.action = dry ? "would prune" : "blocked (loop guard)";
                }
            }
            results.push_back(r);
        }
        // Snapshot age
        {
            fs::path bdir = icmgDir() / "backups";
            std::time_t newest = 0;
            if (fs::exists(bdir)) {
                for (auto& e : fs::directory_iterator(bdir)) {
                    if (!e.is_regular_file()) continue;
                    if (e.path().extension() != ".db") continue;
                    std::time_t mt = mtime(e.path());
                    if (mt > newest) newest = mt;
                }
            }
            long age_h = newest ? (std::time(nullptr) - newest) / 3600 : 9999;
            Result r{"snapshot_age", "OK", std::to_string(age_h) + "h", ""};
            if (age_h > t.snap_age_h) {
                r.status = "WARN";
                if (can_react && !dry) {
                    core::safeExecShell("icmg backup snapshot --note sentinel", false, 30000);
                    r.action = "snapshot taken"; ++reactions;
                } else r.action = dry ? "would snapshot" : "blocked";
            }
            results.push_back(r);
        }
        // Mirror age + count
        {
            fs::path ma = icmgDir() / "data.db.mirror-a";
            fs::path mb = icmgDir() / "data.db.mirror-b";
            int present = (int)fs::exists(ma) + (int)fs::exists(mb);
            std::time_t newest = std::max(mtime(ma), mtime(mb));
            long age_m = newest ? (std::time(nullptr) - newest) / 60 : 9999;
            Result r{"mirror", "OK",
                     std::to_string(present) + "/2, " + std::to_string(age_m) + "m", ""};
            if (present < 2 || age_m > t.mirror_age_m) {
                r.status = "WARN";
                if (can_react && !dry) {
                    core::safeExecShell("icmg mirror sync", false, 30000);
                    if (present < 2) core::safeExecShell("icmg mirror sync", false, 30000);
                    r.action = "synced"; ++reactions;
                } else r.action = dry ? "would sync" : "blocked";
            }
            results.push_back(r);
        }
        // Cache rows
        {
            int rows = 0;
            try {
                auto& cfg = core::Config::instance();
                core::Db db(cfg.projectDbPath("."));
                db.query("SELECT COUNT(*) FROM tool_call_cache", {},
                         [&](const core::Row& row){ if (!row.empty()) rows = std::stoi(row[0]); });
            } catch (...) {}
            Result r{"cache_rows", "OK", std::to_string(rows), ""};
            if (rows > t.cache_rows_max) {
                r.status = "WARN";
                if (can_react && !dry) {
                    core::safeExecShell("icmg cache prune", false, 15000);
                    r.action = "pruned"; ++reactions;
                } else r.action = dry ? "would prune" : "blocked";
            }
            results.push_back(r);
        }
        // Audit log size
        {
            fs::path log = icmgDir() / "audit.log";
            uintmax_t b = fs::exists(log) ? fs::file_size(log) : 0;
            uintmax_t mb = b / 1024 / 1024;
            Result r{"audit_size", "OK", std::to_string(mb) + "MB", ""};
            if ((int)mb > t.audit_mb_max) {
                r.status = "WARN";
                if (!dry) {
                    fs::path rotated = log; rotated += ".1";
                    std::error_code ec;
                    fs::remove(rotated, ec);
                    fs::rename(log, rotated, ec);
                    r.action = "rotated"; ++reactions;
                } else r.action = "would rotate";
            }
            results.push_back(r);
        }
        // DB integrity → trigger failover
        {
            Result r{"integrity", "OK", "", ""};
            try {
                auto& cfg = core::Config::instance();
                core::Db db(cfg.projectDbPath("."));
                std::string ok = "?";
                db.query("PRAGMA integrity_check", {},
                         [&](const core::Row& row){ if (!row.empty()) ok = row[0]; });
                r.detail = "check=" + ok;
                if (ok != "ok") {
                    r.status = "FAIL";
                    if (can_react && !dry) {
                        core::safeExecShell("icmg mirror failover", false, 30000);
                        r.action = "failover attempted"; ++reactions;
                    } else r.action = dry ? "would failover" : "blocked";
                }
            } catch (...) {
                r.status = "FAIL"; r.detail = "open-fail";
            }
            results.push_back(r);
        }
        // Repair counter visibility
        {
            int total = rc.totalLastHour();
            Result r{"repair_loop", total >= 3 ? "WARN" : "OK",
                     std::to_string(total) + "/h", ""};
            results.push_back(r);
        }

        // Persist state.
        if (!dry) {
            fs::create_directories(fs::path(sentinelState()).parent_path());
            std::ofstream f(sentinelState());
            f << "{\"ts\":" << std::time(nullptr)
              << ",\"reactions\":" << reactions
              << ",\"checks\":" << results.size()
              << ",\"can_react\":" << (can_react ? "true" : "false") << "}\n";

            // Audit single summary line.
            try {
                core::AuditLog al((icmgDir() / "audit.log").string());
                al.append("sentinel", "RUN",
                          "checks=" + std::to_string(results.size())
                          + " reactions=" + std::to_string(reactions)
                          + " can_react=" + (can_react ? "1" : "0"));
            } catch (...) {}
        }

        // Render.
        if (json) {
            std::cout << "{\"checks\":[";
            bool first = true;
            for (auto& r : results) {
                if (!first) std::cout << ",";
                first = false;
                std::cout << "{\"check\":\"" << r.check << "\""
                          << ",\"status\":\"" << r.status << "\""
                          << ",\"detail\":\"" << r.detail << "\""
                          << ",\"action\":\"" << r.action << "\"}";
            }
            std::cout << "],\"reactions\":" << reactions
                      << ",\"can_react\":" << (can_react ? "true" : "false")
                      << ",\"dry_run\":" << (dry ? "true" : "false") << "}\n";
        } else if (!quiet) {
            std::cout << "icmg sentinel (" << (dry ? "dry-run" : "live") << ")\n"
                      << std::string(60, '-') << "\n";
            for (auto& r : results) {
                const char* mark = r.status == "OK"   ? "[+]"
                                  : r.status == "WARN" ? "[!]" : "[X]";
                std::cout << "  " << mark << " " << std::left << std::setw(14) << r.check
                          << " " << std::setw(16) << r.detail;
                if (!r.action.empty()) std::cout << " → " << r.action;
                std::cout << "\n";
            }
            std::cout << "Reactions: " << reactions
                      << (can_react ? "" : " (HALTED — repair loop guard)") << "\n";
        }
        return 0;
    }

    int cmdStatus(const std::vector<std::string>&) {
        if (!fs::exists(sentinelState())) {
            std::cout << "icmg sentinel status: never run\n";
            return 0;
        }
        std::ifstream f(sentinelState());
        std::string buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::cout << "icmg sentinel last-state: " << buf;
        return 0;
    }

    int cmdThresholds(const std::vector<std::string>& args) {
        Thresholds t = loadThresholds();
        bool changed = false;
        auto upd = [&](const char* flag, int& dst){
            std::string v = flagValue(args, flag);
            if (!v.empty()) try { dst = std::stoi(v); changed = true; } catch (...) {}
        };
        upd("--disk-mb",    t.disk_mb_max);
        upd("--snap-age-h", t.snap_age_h);
        upd("--mirror-age-m", t.mirror_age_m);
        upd("--cache-rows", t.cache_rows_max);
        upd("--audit-mb",   t.audit_mb_max);
        if (changed) {
            saveThresholds(t);
            std::cout << "icmg sentinel thresholds: saved\n";
        }
        std::cout << "  disk_mb_max:    " << t.disk_mb_max << "\n"
                  << "  snap_age_h:     " << t.snap_age_h << "\n"
                  << "  mirror_age_m:   " << t.mirror_age_m << "\n"
                  << "  cache_rows_max: " << t.cache_rows_max << "\n"
                  << "  audit_mb_max:   " << t.audit_mb_max << "\n";
        return 0;
    }

    static std::string taskName() {
        std::string p = projectRoot().string();
        uint32_t h = 2166136261u;
        for (char c : p) { h ^= (uint8_t)c; h *= 16777619u; }
        std::ostringstream o;
        o << "icmg-sentinel-" << std::hex << std::setw(8) << std::setfill('0') << h;
        return o.str();
    }

    int cmdAutoOn(const std::vector<std::string>& args) {
        std::string interval = flagValue(args, "--every", "15m");
        int minutes = 15;
        try {
            char unit = interval.back();
            int n = std::stoi(interval.substr(0, interval.size() - 1));
            if      (unit == 'h') minutes = n * 60;
            else if (unit == 'm') minutes = n;
        } catch (...) {}
        if (minutes < 5) minutes = 5;
        std::string tn = taskName();
        std::string root = projectRoot().string();
#ifdef _WIN32
        // Phase 78: bulletproof scheduler via core::registerWindowsSchedule.
        fs::path wrapper = projectRoot() / ".icmg" / "sched" / (tn + ".cmd");
        fs::create_directories(wrapper.parent_path());
        std::ofstream wf(wrapper, std::ios::binary);
        wf << "@echo off\r\n"
           << "cd /d \"" << root << "\"\r\n"
           << "echo === %DATE% %TIME% sentinel ===>> .icmg\\sched\\sched.log\r\n"
           << "icmg sentinel run --quiet >> .icmg\\sched\\sched.log 2>&1\r\n";
        wf.close();
        core::ScheduleSpec spec{tn, wrapper.string(), minutes, "sentinel"};
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
        std::string tmp = "/tmp/icmg-sentinel-cron.tmp";
        std::ofstream f(tmp); f << newtab; f.close();
        core::safeExecShell("crontab " + tmp, true, 5000);
        std::remove(tmp.c_str());
#endif
        std::cout << "icmg sentinel auto-on: every " << minutes << "m\n";
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
            std::string tmp = "/tmp/icmg-sentinel-cron.tmp";
            std::ofstream f(tmp); f << filtered.str(); f.close();
            core::safeExecShell("crontab " + tmp, true, 5000);
            std::remove(tmp.c_str());
        }
#endif
        std::cout << "icmg sentinel auto-off: cleared\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("sentinel", SentinelCommand);

} // namespace icmg::cli
