-- Phase 01: Initial schema — all core tables
-- Migration version: 1

-- ============================================================
-- ICM Memory
-- ============================================================
CREATE TABLE IF NOT EXISTS memory_nodes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    topic       TEXT    NOT NULL,
    content     TEXT    NOT NULL,
    keywords    TEXT,                       -- space-separated
    importance  INTEGER NOT NULL DEFAULT 1, -- 0=low 1=normal 2=high 3=critical
    frequency   INTEGER NOT NULL DEFAULT 1,
    last_used   INTEGER,                    -- unix epoch
    expires_at  INTEGER,                    -- unix epoch, NULL = never
    deleted_at  INTEGER,                    -- soft delete
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);

CREATE INDEX IF NOT EXISTS idx_memory_topic     ON memory_nodes(topic);
CREATE INDEX IF NOT EXISTS idx_memory_last_used ON memory_nodes(last_used);
CREATE INDEX IF NOT EXISTS idx_memory_deleted   ON memory_nodes(deleted_at);

CREATE TABLE IF NOT EXISTS memory_keywords (
    memory_id   INTEGER NOT NULL REFERENCES memory_nodes(id) ON DELETE CASCADE,
    keyword     TEXT    NOT NULL,
    PRIMARY KEY (memory_id, keyword)
);

CREATE TABLE IF NOT EXISTS query_history (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    query       TEXT    NOT NULL,
    matched_ids TEXT,                       -- JSON array
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);

-- ============================================================
-- RTK Commands
-- ============================================================
CREATE TABLE IF NOT EXISTS commands (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    command     TEXT    NOT NULL UNIQUE,
    frequency   INTEGER NOT NULL DEFAULT 1,
    last_used   INTEGER,
    avg_lines   INTEGER NOT NULL DEFAULT 0,
    tags        TEXT                        -- JSON array
);

-- ============================================================
-- Knowledge Graph
-- ============================================================
CREATE TABLE IF NOT EXISTS graph_nodes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    path        TEXT    NOT NULL UNIQUE,
    lang        TEXT,
    context     TEXT,
    symbols     TEXT,                       -- JSON {imports:[],classes:[],functions:[]}
    size_bytes  INTEGER,
    file_hash   TEXT,
    access_count INTEGER NOT NULL DEFAULT 0,
    updated_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);

CREATE INDEX IF NOT EXISTS idx_graph_path      ON graph_nodes(path);
CREATE INDEX IF NOT EXISTS idx_graph_lang      ON graph_nodes(lang);
CREATE INDEX IF NOT EXISTS idx_graph_hash      ON graph_nodes(file_hash);
CREATE INDEX IF NOT EXISTS idx_graph_updated   ON graph_nodes(updated_at);

CREATE TABLE IF NOT EXISTS graph_edges (
    src         INTEGER NOT NULL REFERENCES graph_nodes(id) ON DELETE CASCADE,
    dst         INTEGER NOT NULL REFERENCES graph_nodes(id) ON DELETE CASCADE,
    edge_type   TEXT    NOT NULL, -- imports|calls|inherits|includes
    weight      REAL    NOT NULL DEFAULT 1.0,
    PRIMARY KEY (src, dst, edge_type)
);

CREATE INDEX IF NOT EXISTS idx_graph_edge_src  ON graph_edges(src);
CREATE INDEX IF NOT EXISTS idx_graph_edge_dst  ON graph_edges(dst);

-- ============================================================
-- Per-folder Rules
-- ============================================================
CREATE TABLE IF NOT EXISTS rules (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    scope_path  TEXT    NOT NULL,
    rule_type   TEXT    NOT NULL, -- coding|arch|workflow|model|custom
    name        TEXT    NOT NULL,
    content     TEXT    NOT NULL,
    priority    INTEGER NOT NULL DEFAULT 0,
    active      INTEGER NOT NULL DEFAULT 1,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    UNIQUE(scope_path, rule_type, name)
);

CREATE INDEX IF NOT EXISTS idx_rules_scope ON rules(scope_path);

-- ============================================================
-- Structured Data
-- ============================================================
CREATE TABLE IF NOT EXISTS structured_data (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    data_type   TEXT    NOT NULL, -- model|view|behavior|schema
    name        TEXT    NOT NULL UNIQUE,
    scope_path  TEXT,
    content     TEXT    NOT NULL,
    version     TEXT    NOT NULL DEFAULT '1.0',
    tags        TEXT,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    updated_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);

CREATE TABLE IF NOT EXISTS data_versions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    data_id     INTEGER NOT NULL REFERENCES structured_data(id) ON DELETE CASCADE,
    version     TEXT    NOT NULL,
    content     TEXT    NOT NULL,
    changed_by  TEXT,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);

-- ============================================================
-- Abbreviations
-- ============================================================
CREATE TABLE IF NOT EXISTS abbreviations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    short_form  TEXT    NOT NULL,
    full_form   TEXT    NOT NULL,
    domain      TEXT,
    scope_path  TEXT,
    frequency   INTEGER NOT NULL DEFAULT 0,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    UNIQUE(short_form, domain)
);

CREATE INDEX IF NOT EXISTS idx_abbr_short ON abbreviations(short_form);

-- ============================================================
-- Stored Procedures
-- ============================================================
CREATE TABLE IF NOT EXISTS stored_procedures (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT    NOT NULL,
    db_type         TEXT,                   -- mssql|mysql|postgres|sqlite
    database_name   TEXT,
    content         TEXT    NOT NULL,
    context         TEXT,
    parameters      TEXT,                   -- JSON [{name,type,direction}]
    return_type     TEXT,
    tables_used     TEXT,                   -- JSON array
    sp_dependencies TEXT,                   -- JSON array
    scope_path      TEXT,
    tags            TEXT,
    version         INTEGER NOT NULL DEFAULT 1,
    created_at      INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    updated_at      INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    UNIQUE(name, database_name)
);

CREATE INDEX IF NOT EXISTS idx_sp_name   ON stored_procedures(name);
CREATE INDEX IF NOT EXISTS idx_sp_db     ON stored_procedures(database_name);

CREATE TABLE IF NOT EXISTS sp_versions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    sp_id       INTEGER NOT NULL REFERENCES stored_procedures(id) ON DELETE CASCADE,
    version     INTEGER NOT NULL,
    content     TEXT    NOT NULL,
    change_note TEXT,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
