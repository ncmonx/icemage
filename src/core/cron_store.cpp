#include "cron_store.hpp"
#include "db.hpp"

namespace icmg::core {

CronStore::CronStore(const std::string& db_path) : db_path_(db_path) {
    // Schema applied via global migrations (mig 2). Safety net: ensure table
    // exists in case the caller built without the migrator path.
    Db db(db_path_);
    db.run(
        "CREATE TABLE IF NOT EXISTS cron_jobs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "project_path TEXT NOT NULL,"
        "chore TEXT NOT NULL,"
        "every_min INTEGER NOT NULL,"
        "last_run INTEGER DEFAULT 0,"
        "created_at INTEGER DEFAULT (strftime('%s','now')),"
        "UNIQUE(project_path, chore))");
    db.run(
        "CREATE INDEX IF NOT EXISTS idx_cron_due ON cron_jobs(last_run, every_min)");
}

void CronStore::upsert(const std::string& project_path,
                        const std::string& chore, int every_min) {
    Db db(db_path_);
    // Preserve last_run on existing rows; only update every_min.
    db.run(
        "INSERT INTO cron_jobs (project_path, chore, every_min) "
        "VALUES (?,?,?) "
        "ON CONFLICT(project_path, chore) DO UPDATE SET every_min = excluded.every_min",
        {project_path, chore, std::to_string(every_min)});
}

void CronStore::remove(const std::string& project_path,
                        const std::string& chore) {
    Db db(db_path_);
    db.run("DELETE FROM cron_jobs WHERE project_path = ? AND chore = ?",
           {project_path, chore});
}

void CronStore::removeProject(const std::string& project_path) {
    Db db(db_path_);
    db.run("DELETE FROM cron_jobs WHERE project_path = ?", {project_path});
}

std::vector<CronJob> CronStore::dueJobs(int64_t now) const {
    std::vector<CronJob> out;
    Db db(db_path_);
    // Predicate: (now - last_run) >= every_min*60. Some rows may have
    // last_run = 0 (never fired); they qualify immediately.
    db.query(
        "SELECT id, project_path, chore, every_min, last_run "
        "FROM cron_jobs "
        "WHERE (? - COALESCE(last_run,0)) >= every_min * 60",
        {std::to_string(now)},
        [&](const Row& r){
            CronJob j;
            try { j.id = std::stoll(r[0]); } catch (...) { j.id = 0; }
            j.project_path = r[1];
            j.chore        = r[2];
            try { j.every_min = std::stoi(r[3]); } catch (...) { j.every_min = 0; }
            try { j.last_run  = std::stoll(r[4]); } catch (...) { j.last_run = 0; }
            out.push_back(std::move(j));
        });
    return out;
}

std::vector<CronJob> CronStore::listAll() const {
    std::vector<CronJob> out;
    Db db(db_path_);
    db.query(
        "SELECT id, project_path, chore, every_min, last_run "
        "FROM cron_jobs ORDER BY project_path, chore",
        {},
        [&](const Row& r){
            CronJob j;
            try { j.id = std::stoll(r[0]); } catch (...) { j.id = 0; }
            j.project_path = r[1];
            j.chore        = r[2];
            try { j.every_min = std::stoi(r[3]); } catch (...) { j.every_min = 0; }
            try { j.last_run  = std::stoll(r[4]); } catch (...) { j.last_run = 0; }
            out.push_back(std::move(j));
        });
    return out;
}

void CronStore::markRan(const std::string& project_path,
                         const std::string& chore, int64_t when) {
    Db db(db_path_);
    db.run("UPDATE cron_jobs SET last_run = ? WHERE project_path = ? AND chore = ?",
           {std::to_string(when), project_path, chore});
}

}  // namespace icmg::core
