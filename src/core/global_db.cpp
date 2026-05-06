#include "global_db.hpp"
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
