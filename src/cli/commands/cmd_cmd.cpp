#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../tkil/tkil.hpp"
#include <iostream>
#include <iomanip>

namespace icmg::cli {

// ----------------------------------------------------------------
// icmg cmd suggest [<prefix>] [--limit N] [--json]
// icmg cmd record  <command...>
// icmg cmd list    [--limit N] [--json]
// icmg cmd stats
// ----------------------------------------------------------------
class CmdCommand : public BaseCommand {
public:
    std::string name()        const override { return "cmd"; }
    std::string description() const override { return "Command history, suggestions, and Tkil stats"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg cmd <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  suggest [<prefix>] [--limit N] [--json]   Suggest commands by score\n"
            "  record  <command...>                       Record a command manually\n"
            "  list    [--limit N] [--json]               List all recorded commands\n"
            "  stats                                      Show Tkil token-savings stats\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        tkil::Tkil tkil_exec(db);

        const std::string& sub = args[0];

        if (sub == "suggest") return doSuggest(tkil_exec, args);
        if (sub == "record")  return doRecord(tkil_exec, args);
        if (sub == "list")    return doList(db, args);
        if (sub == "stats")   { tkil_exec.printStats(); return 0; }

        std::cerr << "icmg cmd: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    // ---- suggest -----------------------------------------------
    int doSuggest(tkil::Tkil& tkil_exec, const std::vector<std::string>& args) {
        std::string prefix;
        int limit = 10;
        bool json_out = false;

        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--limit" && i + 1 < args.size()) {
                try { limit = std::stoi(args[++i]); } catch (...) {}
            } else if (args[i] == "--json") {
                json_out = true;
            } else if (args[i][0] != '-') {
                prefix = args[i];
            }
        }

        auto results = tkil_exec.suggest(prefix, limit);

        if (json_out) {
            std::cout << "[\n";
            for (size_t i = 0; i < results.size(); ++i) {
                auto& r = results[i];
                std::cout << "  {"
                    << "\"command\":\"" << escapeJson(r.command) << "\""
                    << ",\"frequency\":" << r.frequency
                    << ",\"score\":" << std::fixed << std::setprecision(1) << r.score
                    << "}";
                if (i + 1 < results.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "]\n";
        } else {
            if (results.empty()) {
                std::cout << "(no commands recorded yet)\n";
                return 0;
            }
            std::cout << std::left;
            std::cout << std::setw(6) << "SCORE"
                      << std::setw(6) << "USES"
                      << "COMMAND\n";
            std::cout << std::string(60, '-') << "\n";
            for (auto& r : results) {
                std::cout << std::fixed << std::setprecision(0)
                          << std::setw(6) << r.score
                          << std::setw(6) << r.frequency
                          << r.command << "\n";
            }
        }
        return 0;
    }

    // ---- record ------------------------------------------------
    int doRecord(tkil::Tkil& tkil_exec, const std::vector<std::string>& args) {
        // Everything after "record" is the command
        std::string cmd;
        for (size_t i = 1; i < args.size(); ++i) {
            if (!cmd.empty()) cmd += " ";
            cmd += args[i];
        }
        if (cmd.empty()) {
            std::cerr << "icmg cmd record: requires <command>\n";
            return 1;
        }
        tkil_exec.recordManual(cmd);
        std::cout << "Recorded: " << cmd << "\n";
        return 0;
    }

    // ---- list --------------------------------------------------
    int doList(core::Db& db, const std::vector<std::string>& args) {
        int limit = 50;
        bool json_out = false;

        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--limit" && i + 1 < args.size()) {
                try { limit = std::stoi(args[++i]); } catch (...) {}
            } else if (args[i] == "--json") {
                json_out = true;
            }
        }

        struct Row { std::string cmd; int freq; int64_t last_used; int orig; int filt; };
        std::vector<Row> rows;

        db.query(
            "SELECT command, frequency, last_used,"
            " COALESCE(total_original_lines,0), COALESCE(total_filtered_lines,0)"
            " FROM commands ORDER BY frequency DESC LIMIT ?",
            {std::to_string(limit)},
            [&](const core::Row& r) {
                Row row;
                if (r.size() > 0) row.cmd = r[0];
                if (r.size() > 1) try { row.freq = std::stoi(r[1]); } catch (...) {}
                if (r.size() > 2) try { row.last_used = std::stoll(r[2]); } catch (...) {}
                if (r.size() > 3) try { row.orig = std::stoi(r[3]); } catch (...) {}
                if (r.size() > 4) try { row.filt = std::stoi(r[4]); } catch (...) {}
                rows.push_back(row);
            });

        if (json_out) {
            std::cout << "[\n";
            for (size_t i = 0; i < rows.size(); ++i) {
                auto& r = rows[i];
                int reduction = (r.orig > 0)
                    ? (int)((1.0 - (double)r.filt / r.orig) * 100)
                    : 0;
                std::cout << "  {"
                    << "\"command\":\"" << escapeJson(r.cmd) << "\""
                    << ",\"frequency\":" << r.freq
                    << ",\"last_used\":" << r.last_used
                    << ",\"reduction_pct\":" << reduction
                    << "}";
                if (i + 1 < rows.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "]\n";
        } else {
            if (rows.empty()) {
                std::cout << "(no commands recorded yet)\n";
                return 0;
            }
            std::cout << std::left
                      << std::setw(6)  << "USES"
                      << std::setw(8)  << "REDUC%"
                      << "COMMAND\n"
                      << std::string(70, '-') << "\n";
            for (auto& r : rows) {
                int reduction = (r.orig > 0)
                    ? (int)((1.0 - (double)r.filt / r.orig) * 100)
                    : 0;
                std::cout << std::setw(6)  << r.freq
                          << std::setw(7)  << reduction << "%"
                          << "  " << r.cmd << "\n";
            }
        }
        return 0;
    }

    // ---- helpers -----------------------------------------------
    static std::string escapeJson(const std::string& s) {
        std::string out;
        for (char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else                out += c;
        }
        return out;
    }
};

ICMG_REGISTER_COMMAND("cmd", CmdCommand);

} // namespace icmg::cli
