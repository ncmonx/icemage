// Phase 52 T1: top-level `icmg health` — single sanity check.
// Distinct from `icmg memory health` (per-table memory diagnostics).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

class HealthOverallCommand : public BaseCommand {
public:
    std::string name()        const override { return "health"; }
    std::string description() const override {
        return "Single sanity check (DB / hooks / version / sidecars / telemetry)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg health [options]\n\n"
            "Options:\n"
            "  --json         Machine-readable output\n"
            "  --quiet        Only print summary line\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool json_out = hasFlag(args, "--json");
        bool quiet    = hasFlag(args, "--quiet");

        struct Check { std::string name; std::string status; std::string detail; };
        std::vector<Check> checks;

        auto& cfg = core::Config::instance();
        std::string db_path = cfg.projectDbPath(".");
        if (!fs::exists(db_path)) {
            checks.push_back({"db", "WARN", "DB not yet created (run icmg init)"});
        } else {
            try {
                core::Db db(db_path);
                std::string ok = "?";
                db.query("PRAGMA integrity_check", {},
                         [&](const core::Row& r){ if (!r.empty()) ok = r[0]; });
                checks.push_back({"db", ok == "ok" ? "OK" : "FAIL", "integrity=" + ok});
                int v = db.userVersion();
                checks.push_back({"schema", v >= 22 ? "OK" : "WARN",
                                  "user_version=" + std::to_string(v) + " (latest=22)"});
                int64_t rows = 0;
                for (auto* tbl : {"tool_invocations", "compression_telemetry", "thinking_telemetry"}) {
                    try {
                        db.query(std::string("SELECT COUNT(*) FROM ") + tbl, {},
                                 [&](const core::Row& r){ if (!r.empty()) rows += std::stoll(r[0]); });
                    } catch (...) {}
                }
                checks.push_back({"telemetry",
                                  rows < 100000 ? "OK" : "WARN",
                                  std::to_string(rows) + " telemetry rows"
                                  + (rows >= 100000 ? " (icmg memory prune-telemetry)" : "")});
            } catch (const std::exception& e) {
                checks.push_back({"db", "FAIL", e.what()});
            }
        }

        // Hooks installed.
        fs::path settings = fs::current_path() / ".claude" / "settings.local.json";
        if (!fs::exists(settings)) {
            checks.push_back({"hooks", "WARN", "no .claude/settings.local.json (run icmg init)"});
        } else {
            int present = 0, total = 4;
            for (auto* f : {"icmg-bash-rewrite.sh", "icmg-shrink-read.sh",
                             "icmg-cap-output.sh", "icmg-sayless-prompt.sh"}) {
                if (fs::exists(fs::current_path() / ".claude" / "hooks" / f)) ++present;
            }
            checks.push_back({"hooks",
                              present == total ? "OK" : (present > 0 ? "WARN" : "FAIL"),
                              std::to_string(present) + "/" + std::to_string(total) + " hook scripts"});
        }

        // Sayless flag.
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        fs::path sayless_flag = fs::path(home ? home : ".") / ".icmg" / "sayless.flag";
        if (fs::exists(sayless_flag)) {
            std::ifstream f(sayless_flag); std::string lvl; std::getline(f, lvl);
            checks.push_back({"sayless", "INFO", "ON (level=" + (lvl.empty() ? "ultra" : lvl) + ")"});
        } else {
            checks.push_back({"sayless", "INFO", "OFF (toggle: icmg sayless on)"});
        }

        // Sidecars.
        fs::path embedder = fs::path(home ? home : ".") / ".icmg" / "embed" / "icmg_embedder.py";
        checks.push_back({"sidecar.embedder",
                          fs::exists(embedder) ? "OK" : "INFO",
                          fs::exists(embedder) ? "installed" : "missing (semantic recall optional)"});

        // Sync state.
        fs::path sync_dir = fs::current_path() / ".icmg" / "sync";
        if (fs::exists(sync_dir)) {
            int files = 0;
            for (auto& e : fs::directory_iterator(sync_dir)) if (e.is_regular_file()) ++files;
            checks.push_back({"sync", "OK", std::to_string(files) + " snapshot files"});
        } else {
            checks.push_back({"sync", "INFO", "not initialized (icmg sync init)"});
        }

        // Maintain heavy/idle hint (Phase 74 T3).
        if (fs::exists(db_path)) {
            try {
                core::Db db(db_path);
                int64_t mem_rows = 0;
                db.query("SELECT COUNT(*) FROM memory_nodes WHERE deleted_at IS NULL", {},
                         [&](const core::Row& r){ if (!r.empty()) mem_rows = std::stoll(r[0]); });
                uintmax_t db_size = fs::file_size(db_path);
                bool heavy = (db_size > 100ull * 1024 * 1024) || (mem_rows > 50000);
                std::string detail = std::to_string(db_size / 1024 / 1024) + "MB / "
                                   + std::to_string(mem_rows) + " rows";
                if (heavy) detail += " (icmg maintain run)";
                checks.push_back({"maintain", heavy ? "WARN" : "OK", detail});
            } catch (...) {}
        }

        // Mirror status (Phase 74 T7).
        {
            fs::path ma = fs::current_path() / ".icmg" / "data.db.mirror-a";
            fs::path mb = fs::current_path() / ".icmg" / "data.db.mirror-b";
            int present = (int)fs::exists(ma) + (int)fs::exists(mb);
            std::time_t newest = 0;
            for (auto& p : {ma, mb}) {
                if (!fs::exists(p)) continue;
                auto ftime = fs::last_write_time(p);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
                std::time_t t = std::chrono::system_clock::to_time_t(sctp);
                if (t > newest) newest = t;
            }
            if (present == 0) {
                checks.push_back({"mirror", "INFO",
                                  "no mirrors (icmg mirror sync)"});
            } else {
                std::time_t now = std::time(nullptr);
                long age_min = newest ? (now - newest) / 60 : 9999;
                std::string detail = std::to_string(present) + "/2 mirror(s), latest "
                                   + std::to_string(age_min) + "m old";
                checks.push_back({"mirror",
                                  (present == 2 && age_min < 60) ? "OK" : "WARN",
                                  detail});
            }
        }

        // Backup status (Phase 74).
        fs::path bdir = fs::current_path() / ".icmg" / "backups";
        if (!fs::exists(bdir)) {
            checks.push_back({"backup", "INFO",
                              "no snapshots (icmg backup snapshot)"});
        } else {
            int n = 0;
            std::time_t newest = 0;
            for (auto& e : fs::directory_iterator(bdir)) {
                if (!e.is_regular_file()) continue;
                if (e.path().extension() != ".db") continue;
                ++n;
                auto ftime = fs::last_write_time(e.path());
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
                std::time_t t = std::chrono::system_clock::to_time_t(sctp);
                if (t > newest) newest = t;
            }
            std::time_t now = std::time(nullptr);
            long age_h = newest ? (now - newest) / 3600 : 9999;
            std::string status = (age_h <= 48) ? "OK" : "WARN";
            std::string detail = std::to_string(n) + " snap(s), latest "
                + std::to_string(age_h) + "h old";
            if (age_h > 48) detail += " (icmg backup auto-on)";
            checks.push_back({"backup", status, detail});
        }

        // Render.
        bool any_fail = false, any_warn = false;
        for (auto& c : checks) {
            if (c.status == "FAIL") any_fail = true;
            else if (c.status == "WARN") any_warn = true;
        }

        if (json_out) {
            nlohmann::json j;
            j["overall"] = any_fail ? "FAIL" : (any_warn ? "WARN" : "OK");
            j["checks"] = nlohmann::json::array();
            for (auto& c : checks) {
                j["checks"].push_back({
                    {"name", c.name}, {"status", c.status}, {"detail", c.detail}
                });
            }
            std::cout << j.dump(2) << "\n";
        } else if (quiet) {
            std::cout << (any_fail ? "FAIL" : (any_warn ? "WARN" : "OK")) << "\n";
        } else {
            std::cout << "icmg health\n"
                      << std::string(60, '-') << "\n";
            for (auto& c : checks) {
                const char* mark = c.status == "OK"   ? "[+]"
                                  : c.status == "FAIL" ? "[X]"
                                  : c.status == "WARN" ? "[!]" : "[i]";
                std::cout << "  " << mark << " " << c.name;
                int pad = 22 - (int)c.name.size();
                if (pad > 0) std::cout << std::string(pad, ' ');
                std::cout << "[" << c.status << "] ";
                int pad2 = 6 - (int)c.status.size();
                if (pad2 > 0) std::cout << std::string(pad2, ' ');
                std::cout << c.detail << "\n";
            }
            std::cout << std::string(60, '-') << "\n"
                      << "Overall: " << (any_fail ? "FAIL" : (any_warn ? "WARN" : "OK")) << "\n";
        }
        return any_fail ? 1 : 0;
    }
};

ICMG_REGISTER_COMMAND("health", HealthOverallCommand);

} // namespace icmg::cli
