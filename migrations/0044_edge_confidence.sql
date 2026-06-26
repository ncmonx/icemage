-- v2.10.0 Phase 1 (graphify parity): edge confidence labels.
-- graph_edges lives in the PROJECT DB, so this migration belongs at the
-- top-level migrations/ dir (project schema, version 44) — NOT migrations/global/.
-- It mirrors the {44} entry in embeddedMigrations() so shipped binaries
-- (which run from the embedded fallback, not the repo migrations/ dir) also
-- gain the column.
--   EXTRACTED  = directly found in source (AST parse, regex, explicit import)
--   INFERRED   = heuristic name-match (call resolution, header guess)
--   AMBIGUOUS  = conflicting / low-confidence resolution
ALTER TABLE graph_edges ADD COLUMN confidence TEXT NOT NULL DEFAULT 'EXTRACTED';
