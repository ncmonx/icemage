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
