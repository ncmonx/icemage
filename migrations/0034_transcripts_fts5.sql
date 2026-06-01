-- v1.21.7 (FB2): transcript FTS5 store.
--
-- Captures session transcripts (or excerpts) so users can full-text search
-- past chats. Recorded by PreCompact hook before the transcript is compacted
-- away. The base `transcripts` table holds canonical rows; `transcripts_fts`
-- is an FTS5 mirror kept in sync via triggers.
--
-- Search via:
--   icmg transcript search "<query>"
--   icmg transcript list / stats

CREATE TABLE IF NOT EXISTS transcripts (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  TEXT NOT NULL,
    content     TEXT NOT NULL,
    char_len    INTEGER NOT NULL DEFAULT 0,
    recorded_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);

CREATE INDEX IF NOT EXISTS idx_transcripts_session  ON transcripts(session_id);
CREATE INDEX IF NOT EXISTS idx_transcripts_recorded ON transcripts(recorded_at);

-- FTS5 mirror. `content_rowid` links FTS rowid to base table id so deletes /
-- updates can cascade. Tokenizer porter+unicode61 mirrors memory_fts.
CREATE VIRTUAL TABLE IF NOT EXISTS transcripts_fts USING fts5(
    content,
    content='transcripts',
    content_rowid='id',
    tokenize='porter unicode61'
);

CREATE TRIGGER IF NOT EXISTS transcripts_ai AFTER INSERT ON transcripts BEGIN
    INSERT INTO transcripts_fts(rowid, content) VALUES (new.id, new.content);
END;
CREATE TRIGGER IF NOT EXISTS transcripts_ad AFTER DELETE ON transcripts BEGIN
    INSERT INTO transcripts_fts(transcripts_fts, rowid, content)
    VALUES('delete', old.id, old.content);
END;
CREATE TRIGGER IF NOT EXISTS transcripts_au AFTER UPDATE ON transcripts BEGIN
    INSERT INTO transcripts_fts(transcripts_fts, rowid, content)
    VALUES('delete', old.id, old.content);
    INSERT INTO transcripts_fts(rowid, content) VALUES (new.id, new.content);
END;
