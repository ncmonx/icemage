// v1.18.0: `icmg whatchanged` — delta vs last session.
//
// Reports new decisions, corrections, memoirs, fixes since `last-stamp.txt`
// timestamp. Useful at session start to see what AI/user did since last
// time. Auto-updates stamp on read unless --peek.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/path_utils.hpp"
#include "../../core/db.hpp"
#include "../../core/config.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

fs::path stampPath() {
    return fs::path(core::icmgGlobalDir()) / "whatchanged.stamp";
}

int64_t readLastStamp() {
    fs::path p = stampPath();
    if (!fs::exists(p)) return 0;
    std::ifstream f(p);
    int64_t ts = 0; f >> ts;
    return ts;
}

void writeLastStamp(int64_t now) {
    fs::path p = stampPath();
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p);
    if (f) f << now << "\n";
}

std::string ago(int64_t now, int64_t ts) {
    if (ts <= 0) return "ever";
    int64_t d = now - ts;
    if (d < 60) return std::to_string(d) + "s";
    if (d < 3600) return std::to_string(d / 60) + "m";
    if (d < 86400) return std::to_string(d / 3600) + "h";
    return std::to_string(d / 86400) + "d";
}

}  // namespace

class WhatChangedCommand : public BaseCommand {
public:
    std::string name()        const override { return "whatchanged"; }
    std::string description() const override {
        return "Delta since last invocation: decisions + corrections + memoirs";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg whatchanged [--peek] [--limit N] [--since SEC]\n\n"
            "Reports new memory entries since last `whatchanged` invocation.\n"
            "Default: auto-updates stamp after report. Use --peek to keep stamp.\n"
            "Flags:\n"
            "  --peek          don't update stamp\n"
            "  --limit N       cap output to N entries (default 30)\n"
            "  --since SEC     override stamp with absolute unix seconds\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool peek = hasFlag(args, "--peek");
        int limit = 30;
        try { limit = std::stoi(flagValue(args, "--limit", "30")); } catch (...) {}

        int64_t since = readLastStamp();
        std::string since_arg = flagValue(args, "--since");
        if (!since_arg.empty()) {
            try { since = std::stoll(since_arg); } catch (...) {}
        }
        int64_t now = (int64_t)std::time(nullptr);

        std::cout << "icmg whatchanged — since " << ago(now, since)
                  << " ago (unix=" << since << ")\n";

        auto& cfg = core::Config::instance();
        try {
            core::Db db(cfg.projectDbPath("."));
            // Memory entries (corrections, decisions, etc.).
            int n_mem = 0;
            db.query(
                "SELECT topic, substr(content, 1, 120), created_at "
                "FROM memory_nodes WHERE created_at > ? "
                "ORDER BY created_at DESC LIMIT ?",
                { std::to_string(since), std::to_string(limit) },
                [&](const core::Row& row) {
                    if (n_mem == 0) std::cout << "\nMemory (" << limit << " max):\n";
                    std::cout << "  [" << row[2] << "] " << row[0] << " — "
                              << row[1] << (row[1].size() >= 120 ? "..." : "") << "\n";
                    ++n_mem;
                });
            if (n_mem == 0) std::cout << "  (no new memory entries)\n";

            // Known-issue fixes (table `failures` or `known_issues` depending on schema).
            int n_fix = 0;
            try {
                db.query(
                    "SELECT pattern, fix, created_at FROM known_issues "
                    "WHERE created_at > ? ORDER BY created_at DESC LIMIT ?",
                    { std::to_string(since), std::to_string(limit) },
                    [&](const core::Row& row) {
                        if (n_fix == 0) std::cout << "\nFixes:\n";
                        std::cout << "  [" << row[2] << "] " << row[0]
                                  << " → " << row[1] << "\n";
                        ++n_fix;
                    });
            } catch (...) {}
            if (n_fix == 0) std::cout << "\nFixes: (none)\n";
        } catch (const std::exception& e) {
            std::cerr << "icmg whatchanged: DB read failed: " << e.what() << "\n";
            return 1;
        }

        if (!peek) writeLastStamp(now);
        return 0;
    }
};

ICMG_REGISTER_COMMAND("whatchanged", WhatChangedCommand);

}  // namespace icmg::cli
