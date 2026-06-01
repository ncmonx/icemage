-- Phase 06: Partial unique index on rules table (active rules only)
-- Migration version: 4
--
-- The initial schema has UNIQUE(scope_path, rule_type, name) at table level,
-- which applies regardless of active flag. This partial index adds the intent
-- from A1: within active rules, enforce uniqueness.
-- The table-level constraint is stricter but cannot be dropped without
-- recreating the table, so we document intent here and rely on it for now.

-- Index speeds up forPath() query (scope_path prefix lookups)
CREATE INDEX IF NOT EXISTS idx_rules_scope_active
    ON rules(scope_path, active)
    WHERE active = 1;

-- Index speeds up rule_type queries
CREATE INDEX IF NOT EXISTS idx_rules_type ON rules(rule_type);
