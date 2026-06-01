// Phase 67 T1: `icmg receipt show` — itemized per-pack token cost report.
//
// pack/context/recall write one row per emitted section to token_receipts.
// receipt show queries last N rows and prints a table.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class ReceiptCommand : public BaseCommand {
public:
    std::string name()        const override { return "receipt"; }
    std::string description() const override {
        return "Show itemized token receipts from recent pack/context/recall calls";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg receipt <action> [options]\n\n"
            "Actions:\n"
            "  show   [--last N]    Print last N rows (default 20)\n"
            "  total  [--window 7d] Aggregate per source within window\n"
            "  by-file [--top N]    Aggregate per label (file/topic) — find hot files\n"
            "  clear  [--older 30d] Delete rows older than window\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        if (action == "show") {
            int limit = 20;
            try { limit = std::stoi(flagValue(args, "--last", "20")); } catch (...) {}
            std::cout << "Recent receipts (last " << limit << "):\n";
            std::cout << "  " << std::left
                      << std::setw(10) << "cmd"
                      << std::setw(12) << "source"
                      << std::setw(8)  << "tok"
                      << "label\n";
            std::cout << "  " << std::string(64, '-') << "\n";
            db.query(
                "SELECT cmd, source, label, est_tokens FROM token_receipts"
                " ORDER BY ts DESC LIMIT ?",
                {std::to_string(limit)},
                [](const core::Row& r) {
                    if (r.size() < 4) return;
                    std::cout << "  " << std::left
                              << std::setw(10) << r[0]
                              << std::setw(12) << r[1]
                              << std::setw(8)  << r[3]
                              << r[2].substr(0, 50) << "\n";
                });
            return 0;
        }

        if (action == "total") {
            std::string win = flagValue(args, "--window", "7d");
            int days = 7;
            if (!win.empty() && win.back() == 'd') {
                try { days = std::stoi(win.substr(0, win.size() - 1)); } catch (...) {}
            }
            int64_t cutoff = (int64_t)std::time(nullptr) - (int64_t)days * 86400;
            std::cout << "Aggregate by source (last " << days << "d):\n";
            std::cout << "  " << std::left
                      << std::setw(14) << "source"
                      << std::setw(10) << "calls"
                      << "tokens\n";
            std::cout << "  " << std::string(40, '-') << "\n";
            db.query(
                "SELECT source, COUNT(*), COALESCE(SUM(est_tokens),0) FROM token_receipts"
                " WHERE ts > ?"
                " GROUP BY source ORDER BY SUM(est_tokens) DESC",
                {std::to_string(cutoff)},
                [](const core::Row& r) {
                    if (r.size() < 3) return;
                    std::cout << "  " << std::left
                              << std::setw(14) << r[0]
                              << std::setw(10) << r[1]
                              << r[2] << "\n";
                });
            return 0;
        }

        if (action == "by-file") {
            int top = 20;
            try { top = std::stoi(flagValue(args, "--top", "20")); } catch (...) {}
            std::cout << "Top " << top << " labels by token cost (all-time):\n";
            std::cout << "  " << std::left
                      << std::setw(10) << "calls"
                      << std::setw(10) << "tokens"
                      << "label\n";
            std::cout << "  " << std::string(60, '-') << "\n";
            db.query(
                "SELECT label, COUNT(*), COALESCE(SUM(est_tokens),0) FROM token_receipts"
                " WHERE label != ''"
                " GROUP BY label ORDER BY SUM(est_tokens) DESC LIMIT ?",
                {std::to_string(top)},
                [](const core::Row& r) {
                    if (r.size() < 3) return;
                    std::cout << "  " << std::left
                              << std::setw(10) << r[1]
                              << std::setw(10) << r[2]
                              << r[0].substr(0, 60) << "\n";
                });
            return 0;
        }

        if (action == "clear") {
            std::string win = flagValue(args, "--older", "30d");
            int days = 30;
            if (!win.empty() && win.back() == 'd') {
                try { days = std::stoi(win.substr(0, win.size() - 1)); } catch (...) {}
            }
            int64_t cutoff = (int64_t)std::time(nullptr) - (int64_t)days * 86400;
            db.run("DELETE FROM token_receipts WHERE ts < ?",
                   {std::to_string(cutoff)});
            std::cout << "icmg receipt: cleared rows older than " << days << "d\n";
            return 0;
        }

        std::cerr << "icmg receipt: unknown action '" << action << "'\n";
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("receipt", ReceiptCommand);

// Helper used by pack/context/recall to write receipt rows.
// Defined here to keep schema knowledge co-located.
void writeTokenReceipt(core::Db& db,
                       const std::string& cmd,
                       const std::string& source,
                       const std::string& label,
                       int est_tokens,
                       int raw_tokens = 0) {
    try {
        db.run("INSERT INTO token_receipts (session_id, cmd, source, label, est_tokens, raw_tokens) "
               "VALUES (?, ?, ?, ?, ?, ?)",
               {"", cmd, source, label.substr(0, 200),
                std::to_string(est_tokens), std::to_string(raw_tokens)});
    } catch (...) {}
}

} // namespace icmg::cli
