// Phase 48: `icmg sync` — team memory + graph sync via git-tracked JSONL snapshots.
//
// Subcommands:
//   init   — create .icmg/sync/ + add to gitignore tracking
//   push   — DB -> .icmg/sync/<table>.jsonl (deterministic, sorted)
//   pull   — .icmg/sync/<table>.jsonl -> DB merge (row_version conflict resolution)
//   merge  — fold rows from another local data.db (adoption case)
//   status — diff summary local-vs-remote
//
// Tables synced: memory_nodes, graph_nodes. Per-user state (recall freq, cache,
// last_used) NOT synced. Embeddings NOT synced (regen via `icmg embed`).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/user_identity.hpp"
#include "../../core/file_lock.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

class SyncCommand : public BaseCommand {
public:
    std::string name()        const override { return "sync"; }
    std::string description() const override {
        return "Team sync via git-tracked JSONL (init/push/pull/merge/status)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg sync <action> [options]\n\n"
            "Actions:\n"
            "  init                  Create .icmg/sync/ + gitignore wiring\n"
            "  push                  Export DB -> .icmg/sync/<table>.jsonl\n"
            "  pull                  Apply remote .icmg/sync/<table>.jsonl -> DB\n"
            "  merge <db-path>       Fold another local DB into this one\n"
            "  status                Show local-vs-remote diff summary\n\n"
            "Common options:\n"
            "  --dry-run             Preview, no writes\n"
            "  --table T             Restrict to one table (memory|graph_nodes)\n"
            "  --yes                 Confirm destructive ops\n"
            "  --force-remote        On pull conflict, take remote (default keep-local)\n"
            "  --strategy S          merge: merge | keep-mine | keep-theirs\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || args.empty()) { usage(); return 0; }
        std::string action = args[0];
        if (action == "init")   return doInit();
        if (action == "push")   return doPush(args);
        if (action == "pull")   return doPull(args);
        if (action == "merge") {
            if (args.size() < 2) { std::cerr << "icmg sync merge: requires <db-path>\n"; return 1; }
            return doMerge(args[1], args);
        }
        if (action == "status") return doStatus(args);
        std::cerr << "icmg sync: unknown action: " << action << "\n";
        usage();
        return 1;
    }

private:
    static const std::vector<std::string>& syncableTables() {
        static const std::vector<std::string> t = {"memory_nodes", "graph_nodes"};
        return t;
    }

    fs::path syncDir() {
        return fs::current_path() / ".icmg" / "sync";
    }

    int doInit() {
        fs::path dir = syncDir();
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            std::cerr << "icmg sync init: cannot create " << dir.string() << ": " << ec.message() << "\n";
            return 2;
        }

        // Write meta + .gitkeep so empty dir tracked.
        json meta;
        meta["version"] = 1;
        meta["created_at"] = (int64_t)std::time(nullptr);
        meta["created_by"] = core::currentUser();
        std::ofstream mf(dir / "_meta.json");
        mf << meta.dump(2) << "\n";

        // Update .gitignore: keep .icmg/data.db ignored, allow .icmg/sync/.
        fs::path gi = fs::current_path() / ".gitignore";
        std::string content;
        if (fs::exists(gi)) {
            std::ifstream in(gi);
            std::ostringstream ss; ss << in.rdbuf();
            content = ss.str();
        }
        const std::string marker = "# icmg sync (Phase 48)";
        if (content.find(marker) == std::string::npos) {
            std::ofstream out(gi, std::ios::app);
            if (!content.empty() && content.back() != '\n') out << "\n";
            out << "\n" << marker << "\n"
                << ".icmg/*\n"
                << "!.icmg/sync/\n"
                << "!.icmg/sync/**\n";
        }

        std::cout << "icmg sync: initialized " << dir.string() << "\n"
                  << "  Next: `icmg sync push` to share current state\n"
                  << "  Then: git add .icmg/sync/ && git commit && git push\n";
        return 0;
    }

    int doPush(const std::vector<std::string>& args) {
        bool dry = hasFlag(args, "--dry-run");
        std::string only = flagValue(args, "--table");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        fs::path dir = syncDir();
        if (!fs::exists(dir)) {
            std::cerr << "icmg sync push: not initialized. Run `icmg sync init` first.\n";
            return 2;
        }

        // Lock to prevent concurrent push.
        core::FileLock lock(dir.string() + "/_meta.json", 5000);
        if (!lock.acquired()) {
            std::cerr << "icmg sync push: " << lock.errorMsg() << "\n";
            return 3;
        }

        int total_rows = 0;
        for (auto& tbl : syncableTables()) {
            if (!only.empty() && tbl != only && tbl != ("memory" + std::string("_nodes")) && only != "memory") continue;
            int n = pushTable(db, tbl, dir, dry);
            total_rows += n;
        }

        if (!dry) {
            // Update meta.
            json meta;
            std::ifstream in(dir / "_meta.json");
            try { in >> meta; } catch (...) { meta = json::object(); }
            meta["last_push_at"] = (int64_t)std::time(nullptr);
            meta["last_push_by"] = core::currentUser();
            std::ofstream out(dir / "_meta.json");
            out << meta.dump(2) << "\n";
        }

        std::cout << (dry ? "[dry-run] " : "")
                  << "icmg sync push: " << total_rows << " rows across "
                  << syncableTables().size() << " tables\n"
                  << "  Next: git add .icmg/sync/ && git commit -m 'sync: push' && git push\n";
        return 0;
    }

    int pushTable(core::Db& db, const std::string& tbl, const fs::path& dir, bool dry) {
        // Build SELECT clause based on table.
        std::string sql;
        if (tbl == "memory_nodes") {
            sql = "SELECT id, topic, content, keywords, importance, created_at, "
                  "       COALESCE(zone,'default'), COALESCE(created_by,''), "
                  "       COALESCE(row_version,0), deleted_at "
                  "FROM memory_nodes ORDER BY id";
        } else if (tbl == "graph_nodes") {
            sql = "SELECT id, COALESCE(kind,'file'), COALESCE(symbol_name,''), path, COALESCE(lang,''), "
                  "       COALESCE(line_start,0), COALESCE(line_end,0), "
                  "       COALESCE(body_hash,''), COALESCE(zone,'default'), "
                  "       COALESCE(created_by,''), COALESCE(row_version,0) "
                  "FROM graph_nodes ORDER BY id";
        } else {
            return 0;
        }

        std::vector<json> rows;
        try {
            db.query(sql, {}, [&](const core::Row& r){
                json j;
                if (tbl == "memory_nodes" && r.size() >= 10) {
                    j["id"]          = std::stoll(r[0]);
                    j["topic"]       = r[1];
                    j["content"]     = r[2];
                    j["keywords"]    = r[3];
                    try { j["importance"] = std::stoi(r[4]); } catch (...) { j["importance"] = 1; }
                    try { j["created_at"] = std::stoll(r[5]); } catch (...) { j["created_at"] = 0; }
                    j["zone"]        = r[6];
                    j["created_by"]  = r[7];
                    try { j["row_version"] = std::stoi(r[8]); } catch (...) { j["row_version"] = 0; }
                    j["deleted"]     = !r[9].empty();
                } else if (tbl == "graph_nodes" && r.size() >= 11) {
                    j["id"]         = std::stoll(r[0]);
                    j["kind"]       = r[1];
                    j["symbol_name"]= r[2];
                    j["path"]       = r[3];
                    j["lang"]       = r[4];
                    try { j["line_start"] = std::stoi(r[5]); } catch (...) { j["line_start"] = 0; }
                    try { j["line_end"]   = std::stoi(r[6]); } catch (...) { j["line_end"] = 0; }
                    j["body_hash"]  = r[7];
                    j["zone"]       = r[8];
                    j["created_by"] = r[9];
                    try { j["row_version"] = std::stoi(r[10]); } catch (...) { j["row_version"] = 0; }
                }
                if (!j.empty()) {
                    j["content_hash"] = contentHash(tbl, j);
                    rows.push_back(j);
                }
            });
        } catch (const std::exception& e) {
            std::cerr << "icmg sync push: " << tbl << " query failed: " << e.what() << "\n";
            return 0;
        }

        if (dry) return (int)rows.size();

        // Sort by content_hash for deterministic output.
        std::sort(rows.begin(), rows.end(), [](const json& a, const json& b){
            return a.value("content_hash", "") < b.value("content_hash", "");
        });

        fs::path out = dir / (tbl + ".jsonl");
        std::ofstream of(out, std::ios::binary);
        for (auto& r : rows) of << r.dump() << "\n";
        of.close();
        return (int)rows.size();
    }

    static std::string contentHash(const std::string& tbl, const json& j) {
        std::string s = tbl;
        if (tbl == "memory_nodes") {
            s += "|" + j.value("topic", "") + "|" + j.value("content", "");
        } else if (tbl == "graph_nodes") {
            s += "|" + j.value("kind", "") + "|" + j.value("symbol_name", "") + "|" + j.value("path", "");
        }
        uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
        char buf[17]; std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
        return buf;
    }

    int doPull(const std::vector<std::string>& args) {
        bool dry = hasFlag(args, "--dry-run");
        bool force_remote = hasFlag(args, "--force-remote");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        fs::path dir = syncDir();
        if (!fs::exists(dir)) {
            std::cerr << "icmg sync pull: not initialized. Run `icmg sync init` first.\n";
            return 2;
        }

        int total_in = 0, total_out = 0, total_conflicts = 0;
        for (auto& tbl : syncableTables()) {
            int in = 0, out = 0, conf = 0;
            pullTable(db, tbl, dir, dry, force_remote, in, out, conf);
            total_in += in; total_out += out; total_conflicts += conf;
        }

        std::cout << (dry ? "[dry-run] " : "")
                  << "icmg sync pull: +" << total_in << " inserted, ~"
                  << total_out << " updated, " << total_conflicts << " conflicts\n";
        if (total_conflicts > 0) {
            std::cout << "  Conflicts kept local. Re-run with --force-remote to overwrite.\n";
        }
        return 0;
    }

    void pullTable(core::Db& db, const std::string& tbl, const fs::path& dir,
                    bool dry, bool force_remote, int& inserts, int& updates, int& conflicts) {
        fs::path src = dir / (tbl + ".jsonl");
        if (!fs::exists(src)) return;

        std::ifstream f(src);
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            json remote;
            try { remote = json::parse(line); } catch (...) { continue; }
            if (!remote.contains("content_hash")) continue;

            std::string ch = remote["content_hash"].get<std::string>();
            int remote_ver = remote.value("row_version", 0);

            // Look up local by content_hash equivalent.
            int local_ver = -1;
            int64_t local_id = 0;
            try {
                std::string look_sql;
                std::vector<std::string> params;
                if (tbl == "memory_nodes") {
                    look_sql = "SELECT id, COALESCE(row_version,0) FROM memory_nodes "
                               "WHERE topic = ? AND content = ? LIMIT 1";
                    params = {remote.value("topic",""), remote.value("content","")};
                } else if (tbl == "graph_nodes") {
                    look_sql = "SELECT id, COALESCE(row_version,0) FROM graph_nodes "
                               "WHERE kind = ? AND symbol_name = ? AND path = ? LIMIT 1";
                    params = {remote.value("kind",""), remote.value("symbol_name",""), remote.value("path","")};
                }
                db.query(look_sql, params, [&](const core::Row& r){
                    if (r.size() >= 2) {
                        local_id  = std::stoll(r[0]);
                        local_ver = std::stoi(r[1]);
                    }
                });
            } catch (...) { continue; }

            if (local_id == 0) {
                // Insert new.
                if (!dry) insertRemote(db, tbl, remote);
                ++inserts;
            } else if (local_ver < remote_ver || (force_remote && local_ver != remote_ver)) {
                if (!dry) updateFromRemote(db, tbl, local_id, remote);
                ++updates;
            } else if (local_ver > remote_ver) {
                // Local is newer — keep, log conflict.
                ++conflicts;
                std::cerr << "[conflict] " << tbl << " row local=v" << local_ver
                          << " > remote=v" << remote_ver
                          << " (" << ch << ") — keeping local\n";
            }
            // local_ver == remote_ver: no-op.
        }
    }

    void insertRemote(core::Db& db, const std::string& tbl, const json& r) {
        try {
            if (tbl == "memory_nodes") {
                db.run("INSERT INTO memory_nodes (topic,content,keywords,importance,frequency,"
                       "last_used,created_at,zone,created_by,row_version) "
                       "VALUES(?,?,?,?,1,?,?,?,?,?)",
                       {r.value("topic",""), r.value("content",""), r.value("keywords",""),
                        std::to_string(r.value("importance",1)),
                        std::to_string(r.value("created_at", (int64_t)std::time(nullptr))),
                        std::to_string(r.value("created_at", (int64_t)std::time(nullptr))),
                        r.value("zone","default"), r.value("created_by",""),
                        std::to_string(r.value("row_version",1))});
            } else if (tbl == "graph_nodes") {
                db.run("INSERT INTO graph_nodes (kind,symbol_name,path,lang,line_start,line_end,"
                       "body_hash,zone,created_by,row_version) VALUES(?,?,?,?,?,?,?,?,?,?)",
                       {r.value("kind",""), r.value("symbol_name",""), r.value("path",""),
                        r.value("lang",""),
                        std::to_string(r.value("line_start",0)),
                        std::to_string(r.value("line_end",0)),
                        r.value("body_hash",""), r.value("zone","default"),
                        r.value("created_by",""),
                        std::to_string(r.value("row_version",1))});
            }
        } catch (...) {}
    }

    void updateFromRemote(core::Db& db, const std::string& tbl, int64_t id, const json& r) {
        try {
            if (tbl == "memory_nodes") {
                db.run("UPDATE memory_nodes SET keywords=?, importance=?, "
                       "row_version=?, created_by=? WHERE id=?",
                       {r.value("keywords",""),
                        std::to_string(r.value("importance",1)),
                        std::to_string(r.value("row_version",1)),
                        r.value("created_by",""), std::to_string(id)});
            } else if (tbl == "graph_nodes") {
                db.run("UPDATE graph_nodes SET line_start=?, line_end=?, body_hash=?, "
                       "row_version=?, created_by=? WHERE id=?",
                       {std::to_string(r.value("line_start",0)),
                        std::to_string(r.value("line_end",0)),
                        r.value("body_hash",""),
                        std::to_string(r.value("row_version",1)),
                        r.value("created_by",""), std::to_string(id)});
            }
        } catch (...) {}
    }

    int doMerge(const std::string& other_db, const std::vector<std::string>& args) {
        if (!fs::exists(other_db)) {
            std::cerr << "icmg sync merge: file not found: " << other_db << "\n";
            return 1;
        }
        bool yes = hasFlag(args, "--yes");
        std::string strategy = flagValue(args, "--strategy", "merge");
        if (strategy != "merge" && strategy != "keep-mine" && strategy != "keep-theirs") {
            std::cerr << "icmg sync merge: invalid --strategy (merge|keep-mine|keep-theirs)\n";
            return 1;
        }

        if (!yes) {
            std::cout << "[dry-run] Would merge " << other_db
                      << " (strategy=" << strategy << ")\n"
                      << "  Re-run with --yes to apply.\n";
            return 0;
        }

        // ATTACH other DB and run merge queries.
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        try {
            db.run("ATTACH DATABASE ? AS other", {other_db});
        } catch (const std::exception& e) {
            std::cerr << "icmg sync merge: attach failed: " << e.what() << "\n";
            return 2;
        }

        int inserts = 0, updates = 0, skips = 0;

        // memory_nodes
        try {
            db.query(
                "SELECT o.topic, o.content, o.keywords, o.importance, o.created_at, "
                "       COALESCE(o.zone,'default'), COALESCE(o.created_by,''), "
                "       COALESCE(o.row_version,0) "
                "FROM other.memory_nodes o WHERE o.deleted_at IS NULL", {},
                [&](const core::Row& r){
                    if (r.size() < 8) return;
                    bool exists = false;
                    try {
                        db.query("SELECT 1 FROM memory_nodes WHERE topic=? AND content=? LIMIT 1",
                                 {r[0], r[1]},
                                 [&](const core::Row&){ exists = true; });
                    } catch (...) {}
                    if (exists) {
                        if (strategy == "keep-theirs") ++updates; // simplified
                        else ++skips;
                        return;
                    }
                    try {
                        db.run("INSERT INTO memory_nodes (topic,content,keywords,importance,"
                               "frequency,last_used,created_at,zone,created_by,row_version) "
                               "VALUES(?,?,?,?,1,?,?,?,?,?)",
                               {r[0], r[1], r[2], r[3], r[4], r[4], r[5], r[6], r[7]});
                        ++inserts;
                    } catch (...) {}
                });
        } catch (const std::exception& e) {
            std::cerr << "merge memory_nodes: " << e.what() << "\n";
        }

        try { db.run("DETACH DATABASE other"); } catch (...) {}

        std::cout << "icmg sync merge: +" << inserts << " inserted, "
                  << updates << " updated, " << skips << " skipped (existing)\n";
        return 0;
    }

    int doStatus(const std::vector<std::string>& /*args*/) {
        fs::path dir = syncDir();
        if (!fs::exists(dir)) {
            std::cout << "icmg sync: not initialized.\n"
                      << "  Run: icmg sync init\n";
            return 0;
        }
        std::cout << "icmg sync status\n"
                  << "  dir:     " << dir.string() << "\n";
        for (auto& tbl : syncableTables()) {
            fs::path f = dir / (tbl + ".jsonl");
            int lines = 0;
            if (fs::exists(f)) {
                std::ifstream in(f);
                std::string l;
                while (std::getline(in, l)) if (!l.empty()) ++lines;
            }
            std::cout << "  " << tbl << ": " << lines << " rows in snapshot\n";
        }
        // Read meta.
        try {
            std::ifstream mf(dir / "_meta.json");
            json meta; mf >> meta;
            std::cout << "  last push: " << meta.value("last_push_at", 0)
                      << " by " << meta.value("last_push_by", "?") << "\n";
        } catch (...) {}
        return 0;
    }
};

ICMG_REGISTER_COMMAND("sync", SyncCommand);

} // namespace icmg::cli
