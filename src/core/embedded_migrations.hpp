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
        {21, R"SQL(
-- 0021_resilience_drift (Phase 75)
-- Decision anchors — drift-check matches incoming prompts against pinned stances.
-- "supersedes" is explicit; recency alone never overrides.
CREATE TABLE IF NOT EXISTS decisions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    topic       TEXT    NOT NULL,
    stance      TEXT    NOT NULL,
    rationale   TEXT    NOT NULL DEFAULT '',
    keywords    TEXT    NOT NULL DEFAULT '',
    pinned      INTEGER NOT NULL DEFAULT 0,
    supersedes  INTEGER REFERENCES decisions(id) ON DELETE SET NULL,
    superseded_at INTEGER,
    made_at     INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    actor       TEXT    NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_decisions_topic    ON decisions(topic);
CREATE INDEX IF NOT EXISTS idx_decisions_pinned   ON decisions(pinned);
CREATE INDEX IF NOT EXISTS idx_decisions_keywords ON decisions(keywords);
-- Memoir pin column for 10× recall boost.
ALTER TABLE memory_nodes ADD COLUMN pinned INTEGER NOT NULL DEFAULT 0;
CREATE INDEX IF NOT EXISTS idx_mem_pinned ON memory_nodes(pinned);
)SQL"},
        {22, R"SQL(
-- 0022_fts5_multiagent (Phase 76)
-- FTS5 virtual table over memory_nodes content; speed BM25 50× on >10K rows.
-- Triggers keep it in sync. Falls back gracefully if FTS5 module absent
-- (CREATE VIRTUAL TABLE returns error → caught by migrator); query layer
-- has a runtime detect of FTS5 availability.
CREATE VIRTUAL TABLE IF NOT EXISTS memory_fts USING fts5(
    topic, content, keywords,
    content='memory_nodes', content_rowid='id',
    tokenize='unicode61 remove_diacritics 2'
);
-- Backfill existing rows.
INSERT INTO memory_fts(rowid, topic, content, keywords)
    SELECT id, topic, content, keywords FROM memory_nodes WHERE deleted_at IS NULL;
-- Sync triggers.
CREATE TRIGGER IF NOT EXISTS memory_fts_ai AFTER INSERT ON memory_nodes BEGIN
    INSERT INTO memory_fts(rowid, topic, content, keywords)
        VALUES (new.id, new.topic, new.content, new.keywords);
END;
CREATE TRIGGER IF NOT EXISTS memory_fts_ad AFTER DELETE ON memory_nodes BEGIN
    INSERT INTO memory_fts(memory_fts, rowid, topic, content, keywords)
        VALUES ('delete', old.id, old.topic, old.content, old.keywords);
END;
CREATE TRIGGER IF NOT EXISTS memory_fts_au AFTER UPDATE ON memory_nodes BEGIN
    INSERT INTO memory_fts(memory_fts, rowid, topic, content, keywords)
        VALUES ('delete', old.id, old.topic, old.content, old.keywords);
    INSERT INTO memory_fts(rowid, topic, content, keywords)
        VALUES (new.id, new.topic, new.content, new.keywords);
END;
-- Multi-agent: add agent_id namespace column to tool_call_cache + index.
-- Existing rows retain empty agent_id ("" = global; backward compatible).
ALTER TABLE tool_call_cache ADD COLUMN agent_id TEXT NOT NULL DEFAULT '';
CREATE INDEX IF NOT EXISTS idx_tcc_agent ON tool_call_cache(agent_id, content_hash);
)SQL"},
        {23, R"SQL(
-- 0023_git_sha (Phase 15 gap)
-- Tag memory nodes with git commit SHA at store time.
ALTER TABLE memory_nodes ADD COLUMN git_sha TEXT NOT NULL DEFAULT '';
CREATE INDEX IF NOT EXISTS idx_memory_nodes_git_sha ON memory_nodes(git_sha);
)SQL"},
        {24, R"SQL(
-- 0024_receipt_raw_tokens
-- Add raw_tokens so savings_cmd can compute pack content savings.
ALTER TABLE token_receipts ADD COLUMN raw_tokens INTEGER NOT NULL DEFAULT 0;
)SQL"},
        {25, R"SQL(
-- 0025_context_nodes (v0.42.0)
-- Structured CLAUDE.md sections + skill index for targeted BM25 injection.
CREATE TABLE IF NOT EXISTS context_nodes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    node_key    TEXT    NOT NULL UNIQUE,
    title       TEXT    NOT NULL,
    content     TEXT    NOT NULL,
    source_file TEXT    NOT NULL DEFAULT '',
    tier        TEXT    NOT NULL DEFAULT 'cold',
    tags        TEXT    NOT NULL DEFAULT '[]',
    active      INTEGER NOT NULL DEFAULT 1,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    updated_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_context_nodes_tier   ON context_nodes(tier);
CREATE INDEX IF NOT EXISTS idx_context_nodes_active ON context_nodes(active);
CREATE INDEX IF NOT EXISTS idx_context_nodes_source ON context_nodes(source_file);
)SQL"},
        {26, R"SQL(
-- 0026_rule_trial (v0.42.1)
-- Trial/supersession lifecycle for rules.
ALTER TABLE rules ADD COLUMN supersedes_id   INTEGER;
ALTER TABLE rules ADD COLUMN trial_mode      INTEGER NOT NULL DEFAULT 0;
ALTER TABLE rules ADD COLUMN trial_prompts   INTEGER NOT NULL DEFAULT 0;
ALTER TABLE rules ADD COLUMN trial_threshold INTEGER NOT NULL DEFAULT 5;
CREATE INDEX IF NOT EXISTS idx_rules_trial ON rules(trial_mode) WHERE trial_mode=1;
)SQL"},
        {27, R"SQL(
-- 0027_memory_seen_per_session (v1.1.0 Task 4)
-- Per-session tracking column for `icmg recall --unseen` diff-aware path.
-- TEXT NULL — zero storage cost on rows that never use the feature.
ALTER TABLE memory_nodes ADD COLUMN last_returned_session TEXT;
CREATE INDEX IF NOT EXISTS idx_memory_last_returned_session
    ON memory_nodes(last_returned_session);
)SQL"},
        {28, R"SQL(
-- 0028_skill_chunks
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
)SQL"},
        {29, R"SQL(
-- 0029_focus_chain (v1.3.0)
-- Per-session todo items re-injected at SessionStart/UserPromptSubmit to prevent agent drift.
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
)SQL"},
        {30, R"SQL(
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
)SQL"},
        {31, R"SQL(
-- 0031_token_counts (v1.4.0 T8)
-- Cache icmg tokens results per file path.
-- Future pack --show-tokens consults cache; skip recompute when mtime unchanged.
CREATE TABLE IF NOT EXISTS token_counts (
    path        TEXT PRIMARY KEY,
    tokens      INTEGER NOT NULL,
    bytes       INTEGER NOT NULL,
    mtime       INTEGER NOT NULL,
    computed_at INTEGER DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_token_counts_mtime ON token_counts(mtime);
)SQL"},
        {32, R"SQL(
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
)SQL"},
        {33, R"SQL(
-- 0033_feedbacks_loop (v1.21.1 FB1)
-- User-facing feedback loop. Records predicted vs actual corrections so
-- future similar prediction paths can search past mistakes. Distinct from
-- `feedback` table (recall-reranking weight per node).
CREATE TABLE IF NOT EXISTS feedbacks (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    topic          TEXT NOT NULL,
    predicted      TEXT NOT NULL,
    actual         TEXT NOT NULL,
    note           TEXT,
    applied_count  INTEGER NOT NULL DEFAULT 0,
    created_at     INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_feedbacks_topic   ON feedbacks(topic);
CREATE INDEX IF NOT EXISTS idx_feedbacks_created ON feedbacks(created_at);
)SQL"},
        {34, R"SQL(
-- 0034_transcripts_fts5 (v1.21.7 FB2)
-- Transcript FTS5 store: captures session transcripts before PreCompact
-- discards them so users can full-text search past chats.
CREATE TABLE IF NOT EXISTS transcripts (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  TEXT NOT NULL,
    content     TEXT NOT NULL,
    char_len    INTEGER NOT NULL DEFAULT 0,
    recorded_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_transcripts_session  ON transcripts(session_id);
CREATE INDEX IF NOT EXISTS idx_transcripts_recorded ON transcripts(recorded_at);

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
)SQL"},
        {35, R"SQL(
-- 0035_style_patterns (v1.22.0 SC1)
-- Style-clone pattern store: captures structural layout from a reference
-- UI file so `style-clone apply` can propagate it to N targets without
-- re-reading the reference per-target.
CREATE TABLE IF NOT EXISTS style_patterns (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    name             TEXT NOT NULL UNIQUE,
    source_path      TEXT NOT NULL,
    lang             TEXT NOT NULL,
    layout_tree      TEXT NOT NULL,
    class_tokens     TEXT NOT NULL DEFAULT '',
    structural_hash  TEXT NOT NULL,
    node_count       INTEGER NOT NULL DEFAULT 0,
    applied_count    INTEGER NOT NULL DEFAULT 0,
    created_at       INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    updated_at       INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_style_patterns_name ON style_patterns(name);
CREATE INDEX IF NOT EXISTS idx_style_patterns_hash ON style_patterns(structural_hash);
)SQL"},
        {36, R"SQL(
-- 0036_port_bundles (v1.24.0 P1)
-- Cross-project file bundle metadata. Artifact lives on disk as
-- `.icmg-port` (magic IPRT + version + sha256 + zstd payload); this row
-- tracks bundles created locally for `port list` + telemetry.
CREATE TABLE IF NOT EXISTS port_bundles (
    id                     INTEGER PRIMARY KEY AUTOINCREMENT,
    name                   TEXT NOT NULL UNIQUE,
    source_project         TEXT NOT NULL,
    file_count             INTEGER NOT NULL DEFAULT 0,
    total_bytes_raw        INTEGER NOT NULL DEFAULT 0,
    total_bytes_compressed INTEGER NOT NULL DEFAULT 0,
    artifact_path          TEXT NOT NULL,
    artifact_sha256        TEXT NOT NULL,
    manifest               TEXT NOT NULL,
    applied_count          INTEGER NOT NULL DEFAULT 0,
    created_at             INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_port_bundles_created ON port_bundles(created_at);
CREATE INDEX IF NOT EXISTS idx_port_bundles_sha     ON port_bundles(artifact_sha256);
)SQL"},
        {37, R"SQL(
-- 0037_write_compressions (v1.25.0 W4)
-- Compressed-write telemetry: per-Write bytes_compressed vs bytes_expanded
-- so `icmg savings --layer write` can quantify token-cost reduction.
CREATE TABLE IF NOT EXISTS write_compressions (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    mode             TEXT NOT NULL,
    base_path        TEXT NOT NULL,
    bytes_compressed INTEGER NOT NULL DEFAULT 0,
    bytes_expanded   INTEGER NOT NULL DEFAULT 0,
    ok               INTEGER NOT NULL DEFAULT 1,
    err              TEXT,
    created_at       INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_write_compressions_created ON write_compressions(created_at);
CREATE INDEX IF NOT EXISTS idx_write_compressions_mode    ON write_compressions(mode);
)SQL"},
        {38, R"SQL(
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
)SQL"},
        {39, R"SQL(
-- 0039 graph_fts (v2.0.0 search snapshot): FTS5 over graph_nodes -> fast code search.
-- content=graph_nodes external-content; triggers keep in sync. Graceful if FTS5 absent.
CREATE VIRTUAL TABLE IF NOT EXISTS graph_fts USING fts5(
    path, symbol_name, context, symbols,
    content='graph_nodes', content_rowid='id',
    tokenize='unicode61 remove_diacritics 2'
);
INSERT INTO graph_fts(rowid, path, symbol_name, context, symbols)
    SELECT id, path, symbol_name, context, symbols FROM graph_nodes;
CREATE TRIGGER IF NOT EXISTS graph_fts_ai AFTER INSERT ON graph_nodes BEGIN
    INSERT INTO graph_fts(rowid, path, symbol_name, context, symbols)
        VALUES (new.id, new.path, new.symbol_name, new.context, new.symbols);
END;
CREATE TRIGGER IF NOT EXISTS graph_fts_ad AFTER DELETE ON graph_nodes BEGIN
    INSERT INTO graph_fts(graph_fts, rowid, path, symbol_name, context, symbols)
        VALUES('delete', old.id, old.path, old.symbol_name, old.context, old.symbols);
END;
CREATE TRIGGER IF NOT EXISTS graph_fts_au AFTER UPDATE ON graph_nodes BEGIN
    INSERT INTO graph_fts(graph_fts, rowid, path, symbol_name, context, symbols)
        VALUES('delete', old.id, old.path, old.symbol_name, old.context, old.symbols);
    INSERT INTO graph_fts(rowid, path, symbol_name, context, symbols)
        VALUES (new.id, new.path, new.symbol_name, new.context, new.symbols);
END;
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
        {2, R"SQL(
-- global 0002_cron_jobs (v1.6.0)
CREATE TABLE IF NOT EXISTS cron_jobs (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    project_path TEXT NOT NULL,
    chore        TEXT NOT NULL,
    every_min    INTEGER NOT NULL,
    last_run     INTEGER DEFAULT 0,
    created_at   INTEGER DEFAULT (strftime('%s','now')),
    UNIQUE(project_path, chore)
);
CREATE INDEX IF NOT EXISTS idx_cron_due ON cron_jobs(last_run, every_min);
)SQL"},
        {27, R"SQL(
-- 0027_rule_violations (v1.35.0)
CREATE TABLE IF NOT EXISTS rule_violations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id     TEXT    NOT NULL,
    session_id  TEXT    NOT NULL DEFAULT '',
    ctx         TEXT    NOT NULL DEFAULT '',
    occurred_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_rule_viol_rule    ON rule_violations(rule_id);
CREATE INDEX IF NOT EXISTS idx_rule_viol_session ON rule_violations(rule_id, session_id);
CREATE INDEX IF NOT EXISTS idx_rule_viol_when    ON rule_violations(occurred_at);
)SQL"},
        {28, R"SQL(
-- 0028_intent_cache (v1.37.0)
CREATE TABLE IF NOT EXISTS intent_cache (
    prompt_hash TEXT PRIMARY KEY,
    intent      TEXT NOT NULL,
    source      TEXT NOT NULL,
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    updated_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE TABLE IF NOT EXISTS intent_backfill_queue (
    prompt_hash TEXT PRIMARY KEY,
    prompt_text TEXT NOT NULL,
    queued_at   INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_intent_updated ON intent_cache(updated_at);
)SQL"},
        {29, R"SQL(
-- 0029_amnesia_events (v1.37.0)
CREATE TABLE IF NOT EXISTS amnesia_events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  TEXT NOT NULL DEFAULT '',
    topic       TEXT NOT NULL,
    prior_node  INTEGER,
    matched_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_amnesia_topic   ON amnesia_events(topic);
CREATE INDEX IF NOT EXISTS idx_amnesia_session ON amnesia_events(session_id);
)SQL"},
        {30, R"SQL(
-- 0030_drift_corrections (v1.37.0)
CREATE TABLE IF NOT EXISTS drift_corrections (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id    TEXT NOT NULL DEFAULT '',
    decision_id   INTEGER NOT NULL,
    stance        TEXT NOT NULL,
    contradicted_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    emitted       INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_drift_session ON drift_corrections(session_id);
CREATE INDEX IF NOT EXISTS idx_drift_emitted ON drift_corrections(emitted);
)SQL"},
        {31, R"SQL(
-- 0031_user_personas (v1.41.0)
-- Per-user persona storage. Multi-user single-server: each user keeps
-- own persona preference. Used by chat/agent/ask paths as system-
-- prompt prefix. Storage only — model enforces own content policies.
CREATE TABLE IF NOT EXISTS user_personas (
    user_id     TEXT PRIMARY KEY,
    persona     TEXT NOT NULL DEFAULT '',
    traits      TEXT NOT NULL DEFAULT '',
    created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    updated_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS ix_user_personas_updated ON user_personas(updated_at DESC);
)SQL"},
        {32, R"SQL(
-- v1.48.0: persistent local-LLM chat history. Stored in global.db (cross-
-- project). MUST NOT be merged into project memory_nodes — user constraint.
-- Each turn appends one row; chat_cmd reads recent rows on REPL start to
-- seed history, then appends after each successful infer.
CREATE TABLE IF NOT EXISTS local_llm_chats (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    TEXT NOT NULL,
    session_id TEXT NOT NULL,
    ts         INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    role       TEXT NOT NULL CHECK(role IN ('user','assistant','system')),
    content    TEXT NOT NULL,
    model_id   TEXT,
    tokens_in  INTEGER DEFAULT 0,
    tokens_out INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS ix_local_llm_chats_session
    ON local_llm_chats(user_id, session_id, ts);
CREATE INDEX IF NOT EXISTS ix_local_llm_chats_ts
    ON local_llm_chats(user_id, ts DESC);

-- FTS5 virtual table for BM25 recall over past chat content (cross-session,
-- per-user). Maintained via triggers on local_llm_chats.
CREATE VIRTUAL TABLE IF NOT EXISTS local_llm_chats_fts USING fts5(
    content,
    content='local_llm_chats',
    content_rowid='id'
);

CREATE TRIGGER IF NOT EXISTS local_llm_chats_ai
    AFTER INSERT ON local_llm_chats BEGIN
    INSERT INTO local_llm_chats_fts(rowid, content)
    VALUES (new.id, new.content);
END;

CREATE TRIGGER IF NOT EXISTS local_llm_chats_ad
    AFTER DELETE ON local_llm_chats BEGIN
    INSERT INTO local_llm_chats_fts(local_llm_chats_fts, rowid, content)
    VALUES('delete', old.id, old.content);
END;
)SQL"},
        {33, R"SQL(
-- v1.78.2: recall_cache_persist (write-through + warm-reload).
CREATE TABLE IF NOT EXISTS recall_cache_persist (
    scope_hash  TEXT    NOT NULL,
    key         TEXT    NOT NULL,
    value       BLOB    NOT NULL,
    hit_count   INTEGER NOT NULL DEFAULT 1,
    last_used   INTEGER NOT NULL,
    byte_size   INTEGER NOT NULL,
    PRIMARY KEY (scope_hash, key)
);
CREATE INDEX IF NOT EXISTS idx_rcp_scope_hits
    ON recall_cache_persist(scope_hash, hit_count DESC);
)SQL"},
        {34, R"SQL(
-- v2.0.0 Phase 4: agent_leases (multi-agent conflict-free work claims).
CREATE TABLE IF NOT EXISTS agent_leases (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    scope        TEXT NOT NULL,
    pid          INTEGER NOT NULL,
    host         TEXT NOT NULL DEFAULT '''',
    task         TEXT NOT NULL DEFAULT '''',
    claimed_at   INTEGER NOT NULL DEFAULT (strftime('''%s''','''now''')),
    heartbeat_at INTEGER NOT NULL DEFAULT (strftime('''%s''','''now'''))
);
CREATE INDEX IF NOT EXISTS idx_agent_leases_scope ON agent_leases(scope);
)SQL"},
    };
}

} // namespace icmg::core
