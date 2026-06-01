// v1.6.0: cron_jobs persistence — global.db backing for icmg-service iterator.
// Replaces per-project Windows schtasks. Each registered project chore is one
// row; icmg-service tick fires due rows internally — no scheduled-task bloat.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace icmg::core {

struct CronJob {
    int64_t     id = 0;
    std::string project_path;
    std::string chore;       // CLI cmd, e.g. "backup snapshot", "maintain run"
    int         every_min = 0;
    int64_t     last_run = 0;
};

class CronStore {
public:
    // Opens (or creates) cron_jobs schema in global.db at db_path.
    explicit CronStore(const std::string& db_path);

    // Insert-or-update by (project_path, chore). Sets every_min; preserves
    // last_run if row exists.
    void upsert(const std::string& project_path, const std::string& chore,
                int every_min);

    // Drop one chore.
    void remove(const std::string& project_path, const std::string& chore);

    // Drop all chores for a project (e.g. project dir deleted).
    void removeProject(const std::string& project_path);

    // Returns rows where (now - last_run) >= every_min*60.
    std::vector<CronJob> dueJobs(int64_t now) const;

    // All rows (for `icmg cron list`).
    std::vector<CronJob> listAll() const;

    // Update last_run timestamp.
    void markRan(const std::string& project_path, const std::string& chore,
                  int64_t when);

private:
    std::string db_path_;
};

}  // namespace icmg::core
