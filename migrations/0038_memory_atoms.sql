-- v1.79.0 ICM dual-memory: semantic atom layer derived from memory_nodes.
CREATE TABLE IF NOT EXISTS memory_atoms (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    source_node_id INTEGER NOT NULL,
    content        TEXT    NOT NULL,
    keywords       TEXT    NOT NULL DEFAULT '',
    embedding      BLOB,                       -- nullable: BM25 fallback when null
    zone           TEXT    NOT NULL DEFAULT 'default',
    scope          TEXT    NOT NULL DEFAULT '',
    created_at     INTEGER NOT NULL DEFAULT 0,
    deleted_at     INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_atoms_source ON memory_atoms(source_node_id);
CREATE INDEX IF NOT EXISTS idx_atoms_zone   ON memory_atoms(zone);

CREATE VIRTUAL TABLE IF NOT EXISTS memory_atoms_fts USING fts5(
    content, keywords, content='memory_atoms', content_rowid='id'
);

-- atomize work queue: store() enqueues; worker drains.
CREATE TABLE IF NOT EXISTS memory_atom_queue (
    node_id     INTEGER PRIMARY KEY,           -- one pending entry per node
    enqueued_at INTEGER NOT NULL DEFAULT 0,
    attempts    INTEGER NOT NULL DEFAULT 0
);
