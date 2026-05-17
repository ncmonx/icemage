-- 0032_approaches (v1.4.0 T4)
-- Track which task approaches succeeded vs failed so AI doesn't repeat
-- proven-failed paths or skip proven-success paths.
CREATE TABLE IF NOT EXISTS approaches (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    task       TEXT NOT NULL,
    approach   TEXT NOT NULL,
    outcome    TEXT NOT NULL CHECK(outcome IN ('success','fail','partial')),
    why        TEXT,
    created_at INTEGER DEFAULT (strftime('%s','now')),
    session_id TEXT
);
CREATE INDEX IF NOT EXISTS idx_approaches_task    ON approaches(task);
CREATE INDEX IF NOT EXISTS idx_approaches_outcome ON approaches(outcome);
