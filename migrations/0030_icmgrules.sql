-- 0030_icmgrules (v1.3.0 T11)
-- Project-level durable rule files (.icmgrules/*.md) synced into DB,
-- injected at SessionStart, toggleable per-file.
CREATE TABLE IF NOT EXISTS rules_bank (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    path        TEXT    NOT NULL UNIQUE,
    content     TEXT    NOT NULL,
    tag         TEXT    NOT NULL DEFAULT '',
    active      INTEGER NOT NULL DEFAULT 1,
    updated_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_rules_bank_active ON rules_bank(active, path);
