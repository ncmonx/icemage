-- v2.10.0 Phase 1: edge confidence labels for graphify parity.
-- Adds a `confidence` column to graph_edges so the query engine knows
-- how trustworthy each edge is:
--   EXTRACTED  = directly found in source (AST parse, regex, explicit import)
--   INFERRED   = heuristic name-match (call resolution, header guess)
--   AMBIGUOUS  = conflicting / low-confidence resolution
ALTER TABLE graph_edges ADD COLUMN confidence TEXT NOT NULL DEFAULT 'EXTRACTED';
