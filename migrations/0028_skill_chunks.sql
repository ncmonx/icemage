-- 0028_skill_chunks.sql
CREATE TABLE IF NOT EXISTS skill_chunks (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    skill_id      INTEGER NOT NULL REFERENCES context_nodes(id) ON DELETE CASCADE,
    parent_path   TEXT NOT NULL,
    heading       TEXT NOT NULL,
    content       TEXT NOT NULL,
    token_count   INTEGER DEFAULT 0,
    embedding     BLOB,
    created_at    INTEGER DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_skill_chunks_skill_id ON skill_chunks(skill_id);
CREATE INDEX IF NOT EXISTS idx_skill_chunks_path     ON skill_chunks(parent_path);
