-- 0045: covenant + task store (deterministic cross-session injection)
-- covenant: must-always-hold rules, full-enumeration inject (never BM25-sampled)
-- task: parked work items that survive across sessions + compaction

CREATE TABLE IF NOT EXISTS covenant (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    zone        TEXT    NOT NULL DEFAULT 'default',
    priority    INTEGER NOT NULL DEFAULT 100,
    title       TEXT    NOT NULL,
    body        TEXT    NOT NULL,
    active      INTEGER NOT NULL DEFAULT 1,
    created_at  INTEGER NOT NULL,
    updated_at  INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_covenant_zone_priority ON covenant(zone, priority ASC, id ASC);

CREATE TABLE IF NOT EXISTS task (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    zone        TEXT    NOT NULL DEFAULT 'default',
    status      TEXT    NOT NULL DEFAULT 'todo',
    title       TEXT    NOT NULL,
    detail      TEXT,
    created_at  INTEGER NOT NULL,
    updated_at  INTEGER NOT NULL,
    done_at     INTEGER
);

CREATE INDEX IF NOT EXISTS idx_task_zone_status ON task(zone, status);
