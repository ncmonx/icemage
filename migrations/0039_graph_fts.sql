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
