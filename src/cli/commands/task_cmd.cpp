// 2026-06-26: `icmg task` — parked work items that survive across sessions + compaction.
// Stored in project DB (table: task). Full-enumeration inject (open tasks only), never sampled.
// Subcommands: add | list | show | doing | done | reopen | edit | rm | inject
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/migrator.hpp"
#include <iostream>
#include <sstream>
#include <ctime>

namespace icmg::cli {

class TaskCommand : public BaseCommand {
public:
    std::string name() const override { return "task"; }
    std::string description() const override {
        return "Parked work items that survive across sessions (covenant-task store)"; }
    void usage() const override {
        std::cout <<
            "Usage: icmg task <sub> ...\n"
            "  add <title> [--detail D] [--zone Z]\n"
            "  list [--status todo|doing|done|all] [--zone Z] [--json]\n"
            "  show <id>\n"
            "  doing <id>      status -> doing\n"
            "  done <id>       status -> done\n"
            "  reopen <id>     status -> todo\n"
            "  edit <id> [--title T] [--detail D]\n"
            "  rm <id>\n"
            "  inject [--zone Z] [--max-items N]   Emit open tasks to stdout\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return args.empty() ? 2 : 0; }

        core::Db db(core::Config::instance().projectDbPath("."));
        ensureSchema(db);

        const std::string sub = args[0];

        // ── add ──────────────────────────────────────────────────────────
        if (sub == "add") {
            if (args.size() < 2) {
                std::cerr << "task add: requires <title>\n"; return 1; }
            std::string title  = args[1];
            std::string detail = flagValue(args, "--detail", "");
            std::string zone   = flagValue(args, "--zone", "default");
            long long now = (long long)std::time(nullptr);
            db.run("INSERT INTO task(zone,status,title,detail,created_at,updated_at)"
                   " VALUES(?,?,?,?,?,?)",
                   {zone, "todo", title, detail,
                    std::to_string(now), std::to_string(now)});
            std::cout << "task added\n";
            return 0;
        }

        // ── list ─────────────────────────────────────────────────────────
        if (sub == "list") {
            std::string status = flagValue(args, "--status", "open");
            std::string zone   = flagValue(args, "--zone", "");
            bool json          = hasFlag(args, "--json");
            std::string sql = "SELECT id,zone,status,title,detail FROM task WHERE 1=1";
            std::vector<std::string> params;
            if (status == "open")       { sql += " AND status IN ('todo','doing')"; }
            else if (status != "all")   { sql += " AND status=?"; params.push_back(status); }
            if (!zone.empty())          { sql += " AND zone=?"; params.push_back(zone); }
            sql += " ORDER BY CASE status WHEN 'doing' THEN 0 ELSE 1 END, id ASC";
            bool first = true;
            if (json) std::cout << "[";
            db.query(sql, params, [&](const core::Row& r){
                if (json) {
                    if (!first) std::cout << ",";
                    first = false;
                    std::cout << "{\"id\":" << r[0] << ",\"status\":\"" << r[2]
                              << "\",\"title\":\"" << r[3] << "\"}";
                } else {
                    std::cout << "#" << r[0] << " [" << r[2] << "] " << r[3];
                    if (!r[4].empty()) std::cout << "  — " << r[4].substr(0, 60);
                    std::cout << "\n";
                }
            });
            if (json) std::cout << "]\n";
            return 0;
        }

        // ── show ─────────────────────────────────────────────────────────
        if (sub == "show") {
            if (args.size() < 2) { std::cerr << "task show: requires <id>\n"; return 1; }
            db.query("SELECT id,zone,status,title,detail,created_at,done_at FROM task WHERE id=?",
                     {args[1]}, [](const core::Row& r){
                std::cout << "id:     " << r[0] << "\n"
                          << "zone:   " << r[1] << "\n"
                          << "status: " << r[2] << "\n"
                          << "title:  " << r[3] << "\n";
                if (!r[4].empty()) std::cout << "detail: " << r[4] << "\n";
            });
            return 0;
        }

        // ── status transitions ────────────────────────────────────────────
        if (sub == "doing" || sub == "done" || sub == "reopen") {
            if (args.size() < 2) {
                std::cerr << "task " << sub << ": requires <id>\n"; return 1; }
            long long now = (long long)std::time(nullptr);
            if (sub == "doing") {
                db.run("UPDATE task SET status='doing',updated_at=?,done_at=NULL WHERE id=?",
                       {std::to_string(now), args[1]});
            } else if (sub == "done") {
                db.run("UPDATE task SET status='done',updated_at=?,done_at=? WHERE id=?",
                       {std::to_string(now), std::to_string(now), args[1]});
            } else { // reopen
                db.run("UPDATE task SET status='todo',updated_at=?,done_at=NULL WHERE id=?",
                       {std::to_string(now), args[1]});
            }
            std::cout << "task " << sub << "\n";
            return 0;
        }

        // ── edit ─────────────────────────────────────────────────────────
        if (sub == "edit") {
            if (args.size() < 2) { std::cerr << "task edit: requires <id>\n"; return 1; }
            std::string id     = args[1];
            std::string title  = flagValue(args, "--title",  "");
            std::string detail = flagValue(args, "--detail", "");
            long long now = (long long)std::time(nullptr);
            if (!title.empty())
                db.run("UPDATE task SET title=?,updated_at=? WHERE id=?",
                       {title, std::to_string(now), id});
            if (!detail.empty())
                db.run("UPDATE task SET detail=?,updated_at=? WHERE id=?",
                       {detail, std::to_string(now), id});
            std::cout << "task updated\n";
            return 0;
        }

        // ── rm ───────────────────────────────────────────────────────────
        if (sub == "rm") {
            if (args.size() < 2) { std::cerr << "task rm: requires <id>\n"; return 1; }
            db.run("DELETE FROM task WHERE id=?", {args[1]});
            std::cout << "task removed\n";
            return 0;
        }

        // ── inject ───────────────────────────────────────────────────────
        if (sub == "inject") {
            std::string zone = flagValue(args, "--zone", "");
            int max_items    = std::stoi(flagValue(args, "--max-items", "0"));

            std::string sql =
                "SELECT id,status,title,detail FROM task"
                " WHERE status IN ('todo','doing')";
            std::vector<std::string> params;
            if (!zone.empty()) { sql += " AND zone=?"; params.push_back(zone); }
            sql += " ORDER BY CASE status WHEN 'doing' THEN 0 ELSE 1 END, id ASC";

            struct Row { std::string id, status, title, detail; };
            std::vector<Row> rows;
            db.query(sql, params, [&](const core::Row& r){
                rows.push_back({r[0], r[1], r[2], r.size()>3 ? r[3] : ""});
            });

            if (rows.empty()) return 0;   // silent — no noise for empty store

            int show = (max_items > 0 && max_items < (int)rows.size())
                       ? max_items : (int)rows.size();
            std::cout << "[icmg open tasks — " << rows.size()
                      << " open, carry forward across sessions]\n";
            for (int i = 0; i < show; ++i) {
                std::cout << "- [" << rows[i].status << "] #" << rows[i].id
                          << " " << rows[i].title;
                if (!rows[i].detail.empty())
                    std::cout << "  — " << rows[i].detail.substr(0, 80);
                std::cout << "\n";
            }
            if (show < (int)rows.size()) {
                std::cout << "[+" << (rows.size()-show)
                          << " more tasks not shown — raise --max-items]\n";
            }
            return 0;
        }

        std::cerr << "task: unknown subcommand '" << args[0] << "'\n";
        usage(); return 1;
    }

private:
    void ensureSchema(core::Db& db) {
        db.run(
            "CREATE TABLE IF NOT EXISTS task("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "zone TEXT NOT NULL DEFAULT 'default',"
            "status TEXT NOT NULL DEFAULT 'todo',"
            "title TEXT NOT NULL,"
            "detail TEXT,"
            "created_at INTEGER NOT NULL,"
            "updated_at INTEGER NOT NULL,"
            "done_at INTEGER)");
    }
};

ICMG_REGISTER_COMMAND("task", TaskCommand);

} // namespace icmg::cli
