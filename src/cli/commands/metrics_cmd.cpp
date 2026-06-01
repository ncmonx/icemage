// v1.18.0: `icmg metrics` — runtime observability.
//
// Reports cache hit rates + service health. Useful for tuning dedup
// thresholds, turn_cache TTL, and verifying popup-killer activity.
//
// Subcommands:
//   show       Human-readable dashboard (default)
//   json       Machine-readable JSON
//   reset      Clear all counters

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/inject_dedup.hpp"
#include "../../core/turn_cache.hpp"
#include "../../core/path_utils.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <signal.h>
#endif

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

bool servicePidAlive(long long pid) {
#ifdef _WIN32
    if (pid <= 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return false;
    DWORD ec = 0;
    bool alive = GetExitCodeProcess(h, &ec) && ec == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    return pid > 0 && kill((pid_t)pid, 0) == 0;
#endif
}

long long readPid(const fs::path& p) {
    if (!fs::exists(p)) return 0;
    std::ifstream f(p);
    long long pid = 0; f >> pid;
    return pid;
}

}  // namespace

class MetricsCommand : public BaseCommand {
public:
    std::string name()        const override { return "metrics"; }
    std::string description() const override {
        return "Runtime metrics: cache hit rates + service health";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg metrics [show|json|reset|per-cmd]\n\n"
            "Reports:\n"
            "  inject_dedup unique-hash count\n"
            "  turn_cache hits / misses / hit-rate\n"
            "  service + daemon process state\n"
            "  per-cmd: v1.20.7 — top-10 filtered commands with shrink ratio\n";
    }

    int run(const std::vector<std::string>& args) override {
        std::string sub = args.empty() ? "show" : args[0];
        if (sub == "--help") { usage(); return 0; }

        if (sub == "reset") {
            core::inject_dedup::resetSession();
            core::turn_cache::resetSession();
            std::cout << "icmg metrics: counters reset\n";
            return 0;
        }

        // v1.20.7 (S3): per-cmd shrink stats from project's commands table.
        if (sub == "per-cmd") {
            try {
                core::Db db(core::Config::instance().projectDbPath("."));
                struct Row { std::string cmd; long freq, orig, filt; };
                std::vector<Row> rows;
                db.query(
                    "SELECT command, frequency, "
                    "COALESCE(total_original_lines, 0), "
                    "COALESCE(total_filtered_lines, 0) "
                    "FROM commands "
                    "WHERE total_original_lines > 0 "
                    "ORDER BY (total_original_lines - total_filtered_lines) DESC "
                    "LIMIT 10",
                    {},
                    [&](const std::vector<std::string>& r) {
                        if (r.size() < 4) return;
                        Row row;
                        row.cmd = r[0];
                        try { row.freq = std::stol(r[1]); } catch (...) { row.freq = 0; }
                        try { row.orig = std::stol(r[2]); } catch (...) { row.orig = 0; }
                        try { row.filt = std::stol(r[3]); } catch (...) { row.filt = 0; }
                        rows.push_back(std::move(row));
                    });
                if (rows.empty()) {
                    std::cout << "icmg metrics per-cmd: no filtered commands yet\n";
                    return 0;
                }
                std::cout << "icmg metrics — top-10 filtered commands\n\n";
                std::cout << std::left << std::setw(30) << "command"
                          << std::right << std::setw(7) << "freq"
                          << std::setw(10) << "raw"
                          << std::setw(10) << "filtered"
                          << std::setw(8) << "shrink\n";
                for (auto& row : rows) {
                    int pct = row.orig > 0
                        ? (int)(100.0 - 100.0 * row.filt / row.orig + 0.5) : 0;
                    std::string cmd = row.cmd.substr(0, 28);
                    std::cout << std::left << std::setw(30) << cmd
                              << std::right << std::setw(7) << row.freq
                              << std::setw(10) << row.orig
                              << std::setw(10) << row.filt
                              << std::setw(7) << pct << "%\n";
                }
                return 0;
            } catch (const std::exception& e) {
                std::cerr << "icmg metrics per-cmd: " << e.what() << "\n";
                return 1;
            }
        }

        size_t dedup_n      = core::inject_dedup::uniqueCount();
        size_t cache_hits   = core::turn_cache::hits();
        size_t cache_misses = core::turn_cache::misses();
        size_t cache_total  = cache_hits + cache_misses;
        double cache_rate   = cache_total > 0
            ? (100.0 * cache_hits / cache_total) : 0.0;

        long long svc_pid = readPid(fs::path(core::icmgGlobalDir()) / "service.pid");
        bool svc_alive = servicePidAlive(svc_pid);
        long long dmn_pid = readPid(fs::path(core::icmgGlobalDir()) / "rule-daemon.pid");
        bool dmn_alive = servicePidAlive(dmn_pid);

        if (sub == "json") {
            std::cout << "{"
                      << "\"inject_dedup_unique\":" << dedup_n
                      << ",\"turn_cache_hits\":" << cache_hits
                      << ",\"turn_cache_misses\":" << cache_misses
                      << ",\"turn_cache_hit_rate\":" << cache_rate
                      << ",\"service\":{\"pid\":" << svc_pid
                      << ",\"alive\":" << (svc_alive ? "true" : "false") << "}"
                      << ",\"daemon\":{\"pid\":" << dmn_pid
                      << ",\"alive\":" << (dmn_alive ? "true" : "false") << "}"
                      << "}\n";
            return 0;
        }

        // show (default)
        std::cout << "icmg metrics\n"
                  << "  inject_dedup: " << dedup_n << " unique hashes seen\n"
                  << "  turn_cache:   " << cache_hits << " hits / "
                  << cache_misses << " misses ("
                  << (int)(cache_rate + 0.5) << "% hit-rate)\n"
                  << "  service:      pid=" << svc_pid
                  << " " << (svc_alive ? "ALIVE" : "DEAD") << "\n"
                  << "  rule-daemon:  pid=" << dmn_pid
                  << " " << (dmn_alive ? "ALIVE" : "DEAD") << "\n";
        if (!svc_alive) {
            std::cout << "\nWarning: service not running — popup-killer thread "
                         "inactive. Run `icmg service start`.\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("metrics", MetricsCommand);

}  // namespace icmg::cli
