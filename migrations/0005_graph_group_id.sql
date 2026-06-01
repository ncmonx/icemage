-- Phase 03 amendment: group_id for VS designer file triples
-- .cs + .Designer.cs + .resx files share the same group_id (= canonical .cs path)
-- so the graph can treat them as one logical entity.

ALTER TABLE graph_nodes ADD COLUMN group_id TEXT;

CREATE INDEX IF NOT EXISTS idx_graph_nodes_group ON graph_nodes(group_id)
    WHERE group_id IS NOT NULL;

COMMIT;
