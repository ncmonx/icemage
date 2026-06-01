-- v2.0.0 Phase 4: multi-agent work leases. Agents sharing one global DB claim a
-- work SCOPE (file / zone / task id); a live lease blocks a different owner from
-- claiming the same scope. Reclaimable once heartbeat goes stale (dead owner).
CREATE TABLE IF NOT EXISTS agent_leases (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    scope        TEXT NOT NULL,
    pid          INTEGER NOT NULL,
    host         TEXT NOT NULL DEFAULT '',
    task         TEXT NOT NULL DEFAULT '',
    claimed_at   INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    heartbeat_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_agent_leases_scope ON agent_leases(scope);
