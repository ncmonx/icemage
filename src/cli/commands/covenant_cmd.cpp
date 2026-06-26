// 2026-06-26: `icmg covenant` — deterministic cross-session injection of must-hold rules.
// Stored in project DB (table: covenant). Full-enumeration inject, never BM25-sampled.
// Subcommands: add | list | show | edit | enable | disable | rm | inject
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/migrator.hpp"
#include <iostream>
#include <sstream>
#include <ctime>

namespace icmg::cli {

class CovenantCommand : public BaseCommand {
public:
    std::string name() const override { return "covenant"; }
    std::string description() const override {
        return "Deterministic cross-session rule injection (must-hold covenants)"; }
    void usage() const override {
        std::cout <<
            "Usage: icmg covenant <sub> ...\n"
            "  add <title> <body> [--priority N] [--zone Z]\n"
            "  list [--zone Z] [--all] [--json]\n"
            "  show <id>\n"
            "  edit <id> [--title T] [--body B] [--priority N]\n"
            "  enable <id> | disable <id>\n"
            "  rm <id>\n"
            "  inject [--zone Z] [--max-items N]   Emit all active covenants to stdout\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return args.empty() ? 2 : 0; }

        core::Db db(core::Config::instance().projectDbPath("."));
        ensureSchema(db);

        const std::string sub = args[0];

        // ── add ──────────────────────────────────────────────────────────
        if (sub == "add") {
            if (args.size() < 3) {
                std::cerr << "covenant add: requires <title> <body>\n"; return 1; }
            std::string title    = args[1];
            std::string body     = args[2];
            int         priority = 100;
            std::string zone     = "default";
            for (size_t i = 3; i < args.size(); ++i) {
                if (args[i] == "--priority" && i+1 < args.size()) priority = std::stoi(args[++i]);
                else if (args[i] == "--zone" && i+1 < args.size()) zone = args[++i];
            }
            long long now = (long long)std::time(nullptr);
            db.run("INSERT INTO covenant(zone,priority,title,body,active,created_at,updated_at)"
                   " VALUES(?,?,?,?,1,?,?)",
                   {zone, std::to_string(priority), title, body,
                    std::to_string(now), std::to_string(now)});
            std::cout << "covenant added\n";
            return 0;
        }

        // ── list ─────────────────────────────────────────────────────────
        if (sub == "list") {
            bool all  = hasFlag(args, "--all");
            bool json = hasFlag(args, "--json");
            std::string zone = flagValue(args, "--zone", "");
            std::string sql = "SELECT id,zone,priority,title,body,active FROM covenant WHERE 1=1";
            std::vector<std::string> params;
            if (!all)        { sql += " AND active=1"; }
            if (!zone.empty()){ sql += " AND zone=?"; params.push_back(zone); }
            sql += " ORDER BY priority ASC, id ASC";
            bool first = true;
            if (json) std::cout << "[";
            db.query(sql, params, [&](const core::Row& r){
                if (json) {
                    if (!first) std::cout << ",";
                    first = false;
                    std::cout << "{\"id\":" << r[0] << ",\"zone\":\"" << r[1]
                              << "\",\"priority\":" << r[2] << ",\"title\":\"" << r[3]
                              << "\",\"active\":" << r[5] << "}";
                } else {
                    std::cout << "#" << r[0] << " (p" << r[2] << ") ["
                              << (r[5]=="1"?"on":"off") << "] " << r[3] << "\n";
                }
            });
            if (json) std::cout << "]\n";
            return 0;
        }

        // ── show ─────────────────────────────────────────────────────────
        if (sub == "show") {
            if (args.size() < 2) { std::cerr << "covenant show: requires <id>\n"; return 1; }
            db.query("SELECT id,zone,priority,title,body,active FROM covenant WHERE id=?",
                     {args[1]}, [](const core::Row& r){
                std::cout << "id:       " << r[0] << "\n"
                          << "zone:     " << r[1] << "\n"
                          << "priority: " << r[2] << "\n"
                          << "active:   " << (r[5]=="1"?"yes":"no") << "\n"
                          << "title:    " << r[3] << "\n"
                          << "body:\n"    << r[4] << "\n";
            });
            return 0;
        }

        // ── edit ─────────────────────────────────────────────────────────
        if (sub == "edit") {
            if (args.size() < 2) { std::cerr << "covenant edit: requires <id>\n"; return 1; }
            std::string id = args[1];
            std::string title = flagValue(args, "--title", "");
            std::string body  = flagValue(args, "--body",  "");
            std::string prio  = flagValue(args, "--priority", "");
            long long now = (long long)std::time(nullptr);
            if (!title.empty())
                db.run("UPDATE covenant SET title=?,updated_at=? WHERE id=?",
                       {title, std::to_string(now), id});
            if (!body.empty())
                db.run("UPDATE covenant SET body=?,updated_at=? WHERE id=?",
                       {body, std::to_string(now), id});
            if (!prio.empty())
                db.run("UPDATE covenant SET priority=?,updated_at=? WHERE id=?",
                       {prio, std::to_string(now), id});
            std::cout << "covenant updated\n";
            return 0;
        }

        // ── enable / disable ─────────────────────────────────────────────
        if (sub == "enable" || sub == "disable") {
            if (args.size() < 2) {
                std::cerr << "covenant " << sub << ": requires <id>\n"; return 1; }
            int flag = (sub == "enable") ? 1 : 0;
            long long now = (long long)std::time(nullptr);
            db.run("UPDATE covenant SET active=?,updated_at=? WHERE id=?",
                   {std::to_string(flag), std::to_string(now), args[1]});
            std::cout << "covenant " << sub << "d\n";
            return 0;
        }

        // ── rm ───────────────────────────────────────────────────────────
        if (sub == "rm") {
            if (args.size() < 2) { std::cerr << "covenant rm: requires <id>\n"; return 1; }
            db.run("DELETE FROM covenant WHERE id=?", {args[1]});
            std::cout << "covenant removed\n";
            return 0;
        }

        // ── inject ───────────────────────────────────────────────────────
        if (sub == "inject") {
            std::string zone = flagValue(args, "--zone", "");
            int max_items    = std::stoi(flagValue(args, "--max-items", "0"));

            std::string sql = "SELECT id,priority,title,body FROM covenant WHERE active=1";
            std::vector<std::string> params;
            if (!zone.empty()) { sql += " AND zone=?"; params.push_back(zone); }
            sql += " ORDER BY priority ASC, id ASC";

            struct Row { std::string id, prio, title, body; };
            std::vector<Row> rows;
            db.query(sql, params, [&](const core::Row& r){
                rows.push_back({r[0], r[1], r[2], r[3]});
            });

            if (rows.empty()) return 0;   // silent — no noise for empty store

            int show = (max_items > 0 && max_items < (int)rows.size())
                       ? max_items : (int)rows.size();
            std::cout << "[icmg covenants — " << rows.size()
                      << " active, read in full, must hold this session]\n";
            for (int i = 0; i < show; ++i) {
                std::cout << (i+1) << ". (p" << rows[i].prio << ") " << rows[i].title << "\n"
                          << "   " << rows[i].body << "\n";
            }
            if (show < (int)rows.size()) {
                std::cout << "[+" << (rows.size()-show)
                          << " more covenants not shown — raise --max-items]\n";
            }
            return 0;
        }

        std::cerr << "covenant: unknown subcommand '" << args[0] << "'\n";
        usage(); return 1;
    }

private:
    void ensureSchema(core::Db& db) {
        db.run(
            "CREATE TABLE IF NOT EXISTS covenant("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "zone TEXT NOT NULL DEFAULT 'default',"
            "priority INTEGER NOT NULL DEFAULT 100,"
            "title TEXT NOT NULL,"
            "body TEXT NOT NULL,"
            "active INTEGER NOT NULL DEFAULT 1,"
            "created_at INTEGER NOT NULL,"
            "updated_at INTEGER NOT NULL)");
    }
};

ICMG_REGISTER_COMMAND("covenant", CovenantCommand);

} // namespace icmg::cli
