#include "global_db.hpp"
#include "embedded_migrations.hpp"
#include "path_utils.hpp"
#include "config.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace icmg::core {

GlobalDb& GlobalDb::instance() {
    static GlobalDb inst;
    return inst;
}

// ---- init ------------------------------------------------------------------

void GlobalDb::init() {
    std::string global_dir = icmgGlobalDir();
    fs::create_directories(global_dir);

    std::string db_path = global_dir + "/global.db";
    db_ = std::make_unique<core::Db>(db_path);
    runGlobalMigrations();
}

core::Db& GlobalDb::db() {
    if (!db_) init();
    return *db_;
}

// ---- migrations (A3) -------------------------------------------------------

void GlobalDb::runGlobalMigrations() {
    // Ensure tracking table
    db_->run(
        "CREATE TABLE IF NOT EXISTS global_migrations("
        " version    INTEGER PRIMARY KEY,"
        " applied_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ")"
    );

    // Discover migration files from migrations/global/
    // Resolved relative to binary location — fallback to hardcoded DDL.
    // Hardcode migration 1 DDL (avoids path-discovery complexity):
    int applied = 0;
    db_->query("SELECT COUNT(*) FROM global_migrations WHERE version=1", {},
               [&](const core::Row& r) {
                   if (!r.empty()) try { applied = std::stoi(r[0]); } catch (...) {}
               });

    if (applied == 0) {
        db_->run(
            "CREATE TABLE IF NOT EXISTS projects("
            " id            INTEGER PRIMARY KEY AUTOINCREMENT,"
            " name          TEXT NOT NULL UNIQUE,"
            " path          TEXT NOT NULL,"
            " db_path       TEXT NOT NULL,"
            " description   TEXT NOT NULL DEFAULT '',"
            " registered_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
            ")"
        );
        db_->run("CREATE INDEX IF NOT EXISTS idx_projects_name ON projects(name)");
        db_->run("CREATE INDEX IF NOT EXISTS idx_projects_path ON projects(path)");
        db_->run("INSERT INTO global_migrations(version) VALUES(1)");
    }

    // v1.35.0 R4: rule_violations table.
    int v2 = 0;
    db_->query("SELECT COUNT(*) FROM global_migrations WHERE version=2", {},
               [&](const core::Row& r){ if (!r.empty()) try { v2 = std::stoi(r[0]); } catch (...) {} });
    if (v2 == 0) {
        db_->run("CREATE TABLE IF NOT EXISTS rule_violations("
                 " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " rule_id TEXT NOT NULL,"
                 " session_id TEXT NOT NULL DEFAULT '',"
                 " ctx TEXT NOT NULL DEFAULT '',"
                 " occurred_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
                 ")");
        db_->run("CREATE INDEX IF NOT EXISTS idx_rule_viol_rule    ON rule_violations(rule_id)");
        db_->run("CREATE INDEX IF NOT EXISTS idx_rule_viol_session ON rule_violations(rule_id, session_id)");
        db_->run("INSERT INTO global_migrations(version) VALUES(2)");
    }

    // v1.37.0 C2: intent_cache + backfill queue.
    int v3 = 0;
    db_->query("SELECT COUNT(*) FROM global_migrations WHERE version=3", {},
               [&](const core::Row& r){ if (!r.empty()) try { v3 = std::stoi(r[0]); } catch (...) {} });
    if (v3 == 0) {
        db_->run("CREATE TABLE IF NOT EXISTS intent_cache("
                 " prompt_hash TEXT PRIMARY KEY,"
                 " intent TEXT NOT NULL,"
                 " source TEXT NOT NULL,"
                 " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
                 " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
                 ")");
        db_->run("CREATE TABLE IF NOT EXISTS intent_backfill_queue("
                 " prompt_hash TEXT PRIMARY KEY,"
                 " prompt_text TEXT NOT NULL,"
                 " queued_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
                 ")");
        db_->run("CREATE INDEX IF NOT EXISTS idx_intent_updated ON intent_cache(updated_at)");
        db_->run("INSERT INTO global_migrations(version) VALUES(3)");
    }

    // v1.37.0 A7 scaffold: amnesia_events. Consumer code v1.37.1.
    int v4 = 0;
    db_->query("SELECT COUNT(*) FROM global_migrations WHERE version=4", {},
               [&](const core::Row& r){ if (!r.empty()) try { v4 = std::stoi(r[0]); } catch (...) {} });
    if (v4 == 0) {
        db_->run("CREATE TABLE IF NOT EXISTS amnesia_events("
                 " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " session_id TEXT NOT NULL DEFAULT '',"
                 " topic TEXT NOT NULL,"
                 " prior_node INTEGER,"
                 " matched_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
                 ")");
        db_->run("CREATE INDEX IF NOT EXISTS idx_amnesia_topic   ON amnesia_events(topic)");
        db_->run("CREATE INDEX IF NOT EXISTS idx_amnesia_session ON amnesia_events(session_id)");
        db_->run("INSERT INTO global_migrations(version) VALUES(4)");
    }

    // v1.37.0 drift_corrections scaffold. Consumer code v1.37.1.
    int v5 = 0;
    db_->query("SELECT COUNT(*) FROM global_migrations WHERE version=5", {},
               [&](const core::Row& r){ if (!r.empty()) try { v5 = std::stoi(r[0]); } catch (...) {} });
    if (v5 == 0) {
        db_->run("CREATE TABLE IF NOT EXISTS drift_corrections("
                 " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " session_id TEXT NOT NULL DEFAULT '',"
                 " decision_id INTEGER NOT NULL,"
                 " stance TEXT NOT NULL,"
                 " contradicted_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
                 " emitted INTEGER NOT NULL DEFAULT 0"
                 ")");
        db_->run("CREATE INDEX IF NOT EXISTS idx_drift_session ON drift_corrections(session_id)");
        db_->run("CREATE INDEX IF NOT EXISTS idx_drift_emitted ON drift_corrections(emitted)");
        db_->run("INSERT INTO global_migrations(version) VALUES(5)");
    }

    // v1.41.0 migration 6: user_personas (per-user persona storage).
    // Multi-user single-server — each user keeps own persona+traits used
    // as system-prompt prefix in chat/agent/ask. Storage only; model
    // enforces own content policies.
    int v6 = 0;
    db_->query("SELECT COUNT(*) FROM global_migrations WHERE version=6", {},
               [&](const core::Row& r){ if (!r.empty()) try { v6 = std::stoi(r[0]); } catch (...) {} });
    if (v6 == 0) {
        db_->run("CREATE TABLE IF NOT EXISTS user_personas("
                 " user_id     TEXT PRIMARY KEY,"
                 " persona     TEXT NOT NULL DEFAULT '',"
                 " traits      TEXT NOT NULL DEFAULT '',"
                 " created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
                 " updated_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
                 ")");
        db_->run("CREATE INDEX IF NOT EXISTS ix_user_personas_updated "
                 "ON user_personas(updated_at DESC)");
        db_->run("INSERT INTO global_migrations(version) VALUES(6)");
    }

    // v1.49.0: iterate embeddedGlobalMigrations() for versions >= 7.
    // Earlier hand-written 1-6 retained for backward compat; new migrations
    // (0027+) embedded as SQL strings get applied here automatically.
    for (const auto& [version, sql] : embeddedGlobalMigrations()) {
        if (version < 7) continue;
        int applied_v = 0;
        db_->query("SELECT COUNT(*) FROM global_migrations WHERE version=?",
                   {std::to_string(version)},
                   [&](const core::Row& r) {
                       if (!r.empty()) try { applied_v = std::stoi(r[0]); } catch (...) {}
                   });
        if (applied_v == 0) {
            try {
                db_->run(sql);
                db_->run("INSERT INTO global_migrations(version) VALUES(?)",
                         {std::to_string(version)});
            } catch (...) {
                // Silent — downstream modules use CREATE TABLE IF NOT EXISTS
                // as defensive bootstrap fallback.
            }
        }
    }
}

// ---- row helper ------------------------------------------------------------

Project GlobalDb::fromRow(const core::Row& r) {
    Project p;
    if (r.size() > 0) try { p.id            = std::stoll(r[0]); } catch (...) {}
    if (r.size() > 1) p.name          = r[1];
    if (r.size() > 2) p.path          = r[2];
    if (r.size() > 3) p.db_path       = r[3];
    if (r.size() > 4) p.description   = r[4];
    if (r.size() > 5) try { p.registered_at = std::stoll(r[5]); } catch (...) {}
    return p;
}

// ---- addProject (A1 + A4) --------------------------------------------------

int64_t GlobalDb::addProject(const Project& in) {
    if (!db_) init();

    Project p = in;

    // A4: canonicalize paths
    try {
        p.path = fs::weakly_canonical(expandTilde(p.path)).string();
    } catch (...) {}
    // Normalize separators
    std::replace(p.path.begin(), p.path.end(), '\\', '/');

    // Build db_path from project path if not set
    if (p.db_path.empty()) {
        p.db_path = (fs::path(p.path) / ".icmg" / "data.db").string();
    } else {
        try { p.db_path = fs::weakly_canonical(expandTilde(p.db_path)).string(); }
        catch (...) {}
    }
    std::replace(p.db_path.begin(), p.db_path.end(), '\\', '/');

    // A4: db_path must be within project path
    {
        std::string pp = p.path; if (pp.back() != '/') pp += '/';
        std::string dp = p.db_path;
        if (dp.find(pp) != 0) {
            throw SecurityError(
                "db_path must be within project path.\n"
                "  project: " + p.path + "\n"
                "  db:      " + p.db_path);
        }
    }

    // A1: ownership check (Unix only)
#ifndef _WIN32
    {
        struct stat st;
        if (stat(p.path.c_str(), &st) == 0) {
            if (st.st_uid != getuid()) {
                throw SecurityError(
                    "Project path owned by different user. "
                    "Cross-user project access not allowed.");
            }
        }
    }
#endif

    try {
        db_->run(
            "INSERT INTO projects(name,path,db_path,description,registered_at)"
            " VALUES(?,?,?,?,strftime('%s','now'))",
            {p.name, p.path, p.db_path, p.description});
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        if (msg.find("UNIQUE") != std::string::npos)
            throw std::runtime_error("Project '" + p.name + "' already registered.");
        throw;
    }

    int64_t id = 0;
    db_->query("SELECT last_insert_rowid()", {}, [&](const core::Row& r) {
        if (!r.empty()) try { id = std::stoll(r[0]); } catch (...) {}
    });
    return id;
}

// ---- remove / query --------------------------------------------------------

bool GlobalDb::removeProject(const std::string& name) {
    db_->run("DELETE FROM projects WHERE name=?", {name});
    return true;
}

std::optional<Project> GlobalDb::getProject(const std::string& name) const {
    if (!db_) return std::nullopt;
    std::optional<Project> result;
    db_->query(
        "SELECT id,name,path,db_path,description,registered_at"
        " FROM projects WHERE name=?",
        {name},
        [&](const core::Row& r) { result = fromRow(r); });
    return result;
}

std::vector<Project> GlobalDb::listProjects() const {
    if (!db_) return {};
    std::vector<Project> result;
    db_->query(
        "SELECT id,name,path,db_path,description,registered_at"
        " FROM projects ORDER BY name",
        {},
        [&](const core::Row& r) { result.push_back(fromRow(r)); });
    return result;
}

bool GlobalDb::projectExists(const std::string& name) const {
    return getProject(name).has_value();
}

} // namespace icmg::core
