CREATE TABLE IF NOT EXISTS focus_chain (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  TEXT    NOT NULL,
    todo        TEXT    NOT NULL,
    status      TEXT    NOT NULL DEFAULT 'in' CHECK(status IN ('in','done','blocked')),
    ord         INTEGER NOT NULL,
    created_at  INTEGER DEFAULT (strftime('%s','now')),
    updated_at  INTEGER DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_focus_chain_session ON focus_chain(session_id, ord);
