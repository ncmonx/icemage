-- v1.22.0 (SC1): style-clone pattern store.
--
-- Captures a layout tree extracted from a reference UI file (vue/jsx/tsx/
-- html/svelte). Used by `icmg style-clone apply` to propagate structural
-- style to N targets while preserving each target's data bindings.
--
-- `layout_tree` is a compact JSON serialization of the structural skeleton
-- (tags + classes + attr names + nesting; literal text + bound expressions
-- stripped). `structural_hash` is a Merkle hash for fast "is target already
-- in sync?" checks.

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
