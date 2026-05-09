#pragma once
// Auto-embedded migration SQL — keep in sync with migrations/*.sql
// These are used as fallback when the migrations directory is not found
// (i.e., when the binary is run from a directory other than the repo root).

#include <utility>
#include <vector>
#include <string>

namespace icmg::core {

// {version, sql}
inline std::vector<std::pair<int,std::string>> embeddedMigrations() {
    return {
        {1, R"SQL(
-- 0001_initial_schema
CREATE TABLE IF NOT EXISTS memory_nodes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    topic       TEXT    NOT NULL,
    content     TEXT    NOT NULL,
    keywords    TEXT,
    importance  INTEGER NOT NULL DEFAULT 1,
    frequency   INTEGER NOT NULL DEFAULT 1,
    last_used   INTEGER,
    expires_at  INTEGER,
    deleted_at  INTEGER,
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
    matched_ids TEXT,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);

CREATE TABLE IF NOT EXISTS commands (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    command     TEXT    NOT NULL UNIQUE,
    frequency   INTEGER NOT NULL DEFAULT 1,
    last_used   INTEGER,
    avg_lines   INTEGER NOT NULL DEFAULT 0,
    tags        TEXT
);

CREATE TABLE IF NOT EXISTS graph_nodes (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    path         TEXT    NOT NULL UNIQUE,
    lang         TEXT,
    context      TEXT,
    symbols      TEXT,
    size_bytes   INTEGER,
    file_hash    TEXT,
    access_count INTEGER NOT NULL DEFAULT 0,
    updated_at   INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_graph_path    ON graph_nodes(path);
CREATE INDEX IF NOT EXISTS idx_graph_lang    ON graph_nodes(lang);
CREATE INDEX IF NOT EXISTS idx_graph_hash    ON graph_nodes(file_hash);
CREATE INDEX IF NOT EXISTS idx_graph_updated ON graph_nodes(updated_at);

CREATE TABLE IF NOT EXISTS graph_edges (
    src         INTEGER NOT NULL REFERENCES graph_nodes(id) ON DELETE CASCADE,
    dst         INTEGER NOT NULL REFERENCES graph_nodes(id) ON DELETE CASCADE,
    edge_type   TEXT    NOT NULL,
    weight      REAL    NOT NULL DEFAULT 1.0,
    PRIMARY KEY (src, dst, edge_type)
);
CREATE INDEX IF NOT EXISTS idx_graph_edge_src ON graph_edges(src);
CREATE INDEX IF NOT EXISTS idx_graph_edge_dst ON graph_edges(dst);

CREATE TABLE IF NOT EXISTS rules (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    scope_path  TEXT    NOT NULL,
    rule_type   TEXT    NOT NULL,
    name        TEXT    NOT NULL,
    content     TEXT    NOT NULL,
    priority    INTEGER NOT NULL DEFAULT 0,
    active      INTEGER NOT NULL DEFAULT 1,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    UNIQUE(scope_path, rule_type, name)
);
CREATE INDEX IF NOT EXISTS idx_rules_scope ON rules(scope_path);

CREATE TABLE IF NOT EXISTS structured_data (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    data_type   TEXT    NOT NULL,
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

CREATE TABLE IF NOT EXISTS stored_procedures (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT    NOT NULL,
    db_type         TEXT,
    database_name   TEXT,
    content         TEXT    NOT NULL,
    context         TEXT,
    parameters      TEXT,
    return_type     TEXT,
    tables_used     TEXT,
    sp_dependencies TEXT,
    scope_path      TEXT,
    tags            TEXT,
    version         INTEGER NOT NULL DEFAULT 1,
    created_at      INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    updated_at      INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    UNIQUE(name, database_name)
);
CREATE INDEX IF NOT EXISTS idx_sp_name ON stored_procedures(name);
CREATE INDEX IF NOT EXISTS idx_sp_db   ON stored_procedures(database_name);

CREATE TABLE IF NOT EXISTS sp_versions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    sp_id       INTEGER NOT NULL REFERENCES stored_procedures(id) ON DELETE CASCADE,
    version     INTEGER NOT NULL,
    content     TEXT    NOT NULL,
    change_note TEXT,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
)SQL"},
        {2, R"SQL(
-- 0002_graph_amendments
CREATE TABLE IF NOT EXISTS scan_runs (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    root_path   TEXT    NOT NULL,
    node_count  INTEGER NOT NULL DEFAULT 0,
    edge_count  INTEGER NOT NULL DEFAULT 0,
    created_at  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_scan_runs_created ON scan_runs(created_at);
)SQL"},
        {3, R"SQL(
-- 0003_rtk_commands_budget
ALTER TABLE commands ADD COLUMN total_original_lines INTEGER NOT NULL DEFAULT 0;
ALTER TABLE commands ADD COLUMN total_filtered_lines INTEGER NOT NULL DEFAULT 0;
CREATE INDEX IF NOT EXISTS idx_commands_command   ON commands(command);
CREATE INDEX IF NOT EXISTS idx_commands_last_used ON commands(last_used);
CREATE INDEX IF NOT EXISTS idx_commands_frequency ON commands(frequency DESC);
)SQL"},
        {4, R"SQL(
-- 0004_rules_partial_unique
CREATE INDEX IF NOT EXISTS idx_rules_scope_active
    ON rules(scope_path, active)
    WHERE active = 1;
CREATE INDEX IF NOT EXISTS idx_rules_type ON rules(rule_type);
)SQL"},
        {5, R"SQL(
-- 0005_graph_group_id
-- VS designer file triples (.cs + .Designer.cs + .resx) share group_id.
ALTER TABLE graph_nodes ADD COLUMN group_id TEXT;
CREATE INDEX IF NOT EXISTS idx_graph_nodes_group ON graph_nodes(group_id)
    WHERE group_id IS NOT NULL;
)SQL"},
        {6, R"SQL(
-- 0006_zones
-- Subsystem/layer/bounded-context partitioning for scoped recall + sharper BM25.
ALTER TABLE graph_nodes  ADD COLUMN zone TEXT NOT NULL DEFAULT 'default';
ALTER TABLE memory_nodes ADD COLUMN zone TEXT NOT NULL DEFAULT 'default';
CREATE INDEX IF NOT EXISTS idx_graph_zone  ON graph_nodes(zone);
CREATE INDEX IF NOT EXISTS idx_memory_zone ON memory_nodes(zone);
CREATE TABLE IF NOT EXISTS zone_config (
    zone        TEXT PRIMARY KEY,
    description TEXT,
    path_glob   TEXT,
    color       TEXT,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
INSERT OR IGNORE INTO zone_config(zone, description) VALUES('default', 'Catch-all zone');
)SQL"},
        {7, R"SQL(
-- 0007_symbol_nodes
-- Two-tier graph: symbol nodes (class/function/method/sp) child of file nodes.
ALTER TABLE graph_nodes ADD COLUMN parent_id   INTEGER REFERENCES graph_nodes(id) ON DELETE CASCADE;
ALTER TABLE graph_nodes ADD COLUMN kind        TEXT NOT NULL DEFAULT 'file';
ALTER TABLE graph_nodes ADD COLUMN symbol_name TEXT;
ALTER TABLE graph_nodes ADD COLUMN signature   TEXT;
ALTER TABLE graph_nodes ADD COLUMN line_start  INTEGER;
ALTER TABLE graph_nodes ADD COLUMN line_end    INTEGER;
ALTER TABLE graph_nodes ADD COLUMN body_hash   TEXT;
CREATE INDEX IF NOT EXISTS idx_graph_parent ON graph_nodes(parent_id);
CREATE INDEX IF NOT EXISTS idx_graph_kind   ON graph_nodes(kind);
CREATE INDEX IF NOT EXISTS idx_graph_symbol ON graph_nodes(symbol_name) WHERE symbol_name IS NOT NULL;
)SQL"},
        {8, R"SQL(
-- 0008_workflow
-- verifications/phases/designs for Phase 22 workflow integration.
CREATE TABLE IF NOT EXISTS verifications (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    phase TEXT,
    command TEXT NOT NULL,
    exit_code INTEGER NOT NULL,
    output_hash TEXT,
    output_head TEXT,
    duration_ms INTEGER,
    recorded_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_verif_phase ON verifications(phase);
CREATE INDEX IF NOT EXISTS idx_verif_recorded ON verifications(recorded_at);
CREATE TABLE IF NOT EXISTS phases (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    num TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    goal TEXT,
    plan_path TEXT,
    status TEXT NOT NULL DEFAULT 'pending',
    started_at INTEGER,
    completed_at INTEGER,
    commit_sha TEXT,
    notes TEXT
);
CREATE TABLE IF NOT EXISTS designs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    feature TEXT NOT NULL UNIQUE,
    doc_path TEXT,
    status TEXT NOT NULL DEFAULT 'draft',
    approved_at INTEGER,
    approved_by TEXT,
    notes TEXT
);
)SQL"},
        {9, R"SQL(
-- 0009_token_budget
-- Per-invocation token estimate so `icmg budget` shows savings + hot spots.
CREATE TABLE IF NOT EXISTS tool_invocations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    tool_name TEXT NOT NULL,
    command TEXT,
    raw_bytes INTEGER,
    filtered_bytes INTEGER,
    est_tokens_in INTEGER,
    est_tokens_out INTEGER,
    saved_tokens INTEGER
);
CREATE INDEX IF NOT EXISTS idx_tinv_ts ON tool_invocations(timestamp);
CREATE INDEX IF NOT EXISTS idx_tinv_tool ON tool_invocations(tool_name);
)SQL"},
        {10, R"SQL(
-- 0010_embeddings (Phase 23): semantic recall vectors.
CREATE TABLE IF NOT EXISTS embeddings (
    node_id     INTEGER NOT NULL,
    kind        TEXT    NOT NULL,
    vec         BLOB    NOT NULL,
    dim         INTEGER NOT NULL,
    model       TEXT    NOT NULL DEFAULT 'all-MiniLM-L6-v2',
    body_hash   TEXT    NOT NULL DEFAULT '',
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    PRIMARY KEY (node_id, kind)
);
CREATE INDEX IF NOT EXISTS idx_embeddings_kind ON embeddings(kind);
CREATE INDEX IF NOT EXISTS idx_embeddings_hash ON embeddings(kind, body_hash);
)SQL"},
        {11, R"SQL(
-- 0011_templates (Phase 25): persisted template manifests.
CREATE TABLE IF NOT EXISTS templates (
    name          TEXT    PRIMARY KEY,
    source_path   TEXT    NOT NULL,
    manifest_json TEXT    NOT NULL,
    body_hash     TEXT    NOT NULL DEFAULT '',
    memoir_id     INTEGER,
    created_at    INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    updated_at    INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_templates_source ON templates(source_path);
)SQL"},
        {12, R"SQL(
-- 0012_feedback (Phase 27): recall feedback for reranker bias.
CREATE TABLE IF NOT EXISTS feedback (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id     INTEGER NOT NULL,
    query       TEXT NOT NULL DEFAULT '',
    score       INTEGER NOT NULL,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_feedback_node    ON feedback(node_id);
CREATE INDEX IF NOT EXISTS idx_feedback_created ON feedback(created_at);
)SQL"},
        {13, R"SQL(
-- 0013_compression_glossary
CREATE TABLE IF NOT EXISTS compression_glossary (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    content_hash  TEXT NOT NULL,
    alias         TEXT NOT NULL,
    original      TEXT NOT NULL,
    freq          INTEGER NOT NULL DEFAULT 1,
    created_at    INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_compress_hash_alias ON compression_glossary(content_hash, alias);
CREATE TABLE IF NOT EXISTS compression_telemetry (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    cmd           TEXT NOT NULL,
    bytes_in      INTEGER NOT NULL,
    bytes_out     INTEGER NOT NULL,
    tok_in        INTEGER NOT NULL,
    tok_out       INTEGER NOT NULL,
    elapsed_ms    INTEGER NOT NULL,
    mode          TEXT NOT NULL DEFAULT 'lossless',
    created_at    INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
)SQL"},
        {14, R"SQL(
-- 0014_thinking_telemetry
CREATE TABLE IF NOT EXISTS thinking_telemetry (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    cmd             TEXT NOT NULL,
    task            TEXT NOT NULL DEFAULT '',
    intent          TEXT NOT NULL DEFAULT 'unknown',
    no_think        INTEGER NOT NULL DEFAULT 0,
    concise         INTEGER NOT NULL DEFAULT 0,
    input_bytes     INTEGER NOT NULL DEFAULT 0,
    elapsed_ms      INTEGER NOT NULL DEFAULT 0,
    created_at      INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
)SQL"},
        {15, R"SQL(
-- 0015_tool_call_cache
CREATE TABLE IF NOT EXISTS tool_call_cache (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    cmd          TEXT NOT NULL,
    content_hash TEXT NOT NULL UNIQUE,
    result_blob  TEXT NOT NULL,
    hit_count    INTEGER NOT NULL DEFAULT 0,
    created_at   INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    expires_at   INTEGER NOT NULL
);
)SQL"},
        {16, R"SQL(
-- 0016_fetch_cache
CREATE TABLE IF NOT EXISTS fetch_cache (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    url          TEXT NOT NULL UNIQUE,
    etag         TEXT NOT NULL DEFAULT '',
    content_kind TEXT NOT NULL DEFAULT 'unknown',
    body_reduced TEXT NOT NULL,
    bytes_in     INTEGER NOT NULL DEFAULT 0,
    bytes_out    INTEGER NOT NULL DEFAULT 0,
    hit_count    INTEGER NOT NULL DEFAULT 0,
    fetched_at   INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    expires_at   INTEGER NOT NULL
);
)SQL"},
        {17, R"SQL(
-- 0017_image_cache
CREATE TABLE IF NOT EXISTS image_cache (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    image_hash      TEXT NOT NULL UNIQUE,
    bytes           INTEGER NOT NULL DEFAULT 0,
    ocr_text        TEXT NOT NULL DEFAULT '',
    ocr_confidence  INTEGER NOT NULL DEFAULT 0,
    classify_kind   TEXT NOT NULL DEFAULT 'unknown',
    hit_count       INTEGER NOT NULL DEFAULT 0,
    cached_at       INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    expires_at      INTEGER NOT NULL
);
)SQL"},
        {18, R"SQL(
-- 0018_user_identity
ALTER TABLE memory_nodes ADD COLUMN created_by TEXT NOT NULL DEFAULT '';
ALTER TABLE memory_nodes ADD COLUMN row_version INTEGER NOT NULL DEFAULT 0;
)SQL"},
        {19, R"SQL(
-- 0019_sync_tracking
CREATE TABLE IF NOT EXISTS sync_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    table_name  TEXT NOT NULL,
    op          TEXT NOT NULL,
    rows_in     INTEGER NOT NULL DEFAULT 0,
    rows_out    INTEGER NOT NULL DEFAULT 0,
    conflicts   INTEGER NOT NULL DEFAULT 0,
    elapsed_ms  INTEGER NOT NULL DEFAULT 0,
    actor       TEXT NOT NULL DEFAULT '',
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
ALTER TABLE graph_nodes ADD COLUMN row_version INTEGER NOT NULL DEFAULT 0;
ALTER TABLE graph_nodes ADD COLUMN created_by TEXT NOT NULL DEFAULT '';
)SQL"},
        {20, R"SQL(
-- 0020_token_receipts
CREATE TABLE IF NOT EXISTS token_receipts (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  TEXT NOT NULL DEFAULT '',
    cmd         TEXT NOT NULL,
    source      TEXT NOT NULL,
    label       TEXT NOT NULL DEFAULT '',
    est_tokens  INTEGER NOT NULL DEFAULT 0,
    useful_pct  INTEGER NOT NULL DEFAULT 0,
    ts          INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_receipts_ts ON token_receipts(ts);
CREATE INDEX IF NOT EXISTS idx_receipts_session ON token_receipts(session_id);
)SQL"},
    };
}

// For global.db migrations
inline std::vector<std::pair<int,std::string>> embeddedGlobalMigrations() {
    return {
        {1, R"SQL(
-- global 0001_projects_table
CREATE TABLE IF NOT EXISTS projects (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT    NOT NULL UNIQUE,
    path            TEXT    NOT NULL,
    db_path         TEXT    NOT NULL,
    description     TEXT    NOT NULL DEFAULT '',
    registered_at   INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_projects_name ON projects(name);
CREATE INDEX IF NOT EXISTS idx_projects_path ON projects(path);

CREATE TABLE IF NOT EXISTS global_migrations (
    version     INTEGER PRIMARY KEY,
    applied_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
)SQL"},
    };
}

} // namespace icmg::core
