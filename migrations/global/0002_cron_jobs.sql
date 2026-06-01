-- v1.6.0: cron_jobs table — replaces per-project Windows schtasks.
-- icmg-service tick iterates this table internally; no schtasks bloat.
CREATE TABLE IF NOT EXISTS cron_jobs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_path TEXT NOT NULL,
    chore        TEXT NOT NULL,
    every_min    INTEGER NOT NULL,
    last_run     INTEGER DEFAULT 0,
    created_at   INTEGER DEFAULT (strftime('%s','now')),
    UNIQUE(project_path, chore)
);
CREATE INDEX IF NOT EXISTS idx_cron_due ON cron_jobs(last_run, every_min);
