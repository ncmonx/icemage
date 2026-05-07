# icmg — Master Design Document

**Tool:** `icmg` (ICM-Graph unified CLI)
**Date:** 2026-05-06
**Status:** Approved

---

## Goal

Single C++ binary menggabungkan ICM (memory), KGraph (knowledge graph), dan RTK (command filter) dengan tambahan: per-folder rules, structured data, multi-project registry, abbreviation engine, stored procedure store, visual graph, dan MCP server.

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│                  icmg CLI                        │
├──────────┬──────────┬──────────┬────────────────┤
│ Registry │ Hook Bus │ Migrator │ Config Engine  │
├──────────┴──────────┴──────────┴────────────────┤
│  ICM  │  Graph  │  RTK  │  Rules │  SP  │ Abbr │
├───────────────────────────────────────────────── ┤
│         SQLite (WAL mode) — per-project .db      │
│         ~/.icmg/global.db — registry + prefs     │
└─────────────────────────────────────────────────┘
```

---

## Storage Layout

```
~/.icmg/
├── global.db           # project registry + global memory
└── config.json         # feature flags + extractor config

<project_root>/
└── .icmg/
    ├── data.db         # per-project data
    └── watcher.pid     # daemon PID
```

---

## Schema (per-project data.db)

```sql
-- Memory
CREATE TABLE memory_nodes (
    id INTEGER PRIMARY KEY,
    topic TEXT NOT NULL,
    content TEXT NOT NULL,
    keywords TEXT,
    importance INTEGER DEFAULT 1,
    frequency INTEGER DEFAULT 1,
    last_used INTEGER,
    created_at INTEGER
);

-- Commands (RTK)
CREATE TABLE commands (
    id INTEGER PRIMARY KEY,
    command TEXT UNIQUE NOT NULL,
    frequency INTEGER DEFAULT 1,
    last_used INTEGER,
    avg_lines INTEGER DEFAULT 0,
    tags TEXT
);

-- Graph nodes
CREATE TABLE graph_nodes (
    id INTEGER PRIMARY KEY,
    path TEXT UNIQUE NOT NULL,
    lang TEXT,
    context TEXT,
    symbols TEXT,       -- JSON {imports:[], classes:[], functions:[]}
    size_bytes INTEGER,
    file_hash TEXT,
    updated_at INTEGER
);

-- Graph edges
CREATE TABLE graph_edges (
    src INTEGER REFERENCES graph_nodes(id),
    dst INTEGER REFERENCES graph_nodes(id),
    edge_type TEXT,     -- imports|calls|inherits|includes
    weight REAL DEFAULT 1.0,
    PRIMARY KEY (src, dst, edge_type)
);

-- Per-folder rules
CREATE TABLE rules (
    id INTEGER PRIMARY KEY,
    scope_path TEXT NOT NULL,
    rule_type TEXT NOT NULL,    -- coding|arch|workflow|model|custom
    name TEXT NOT NULL,
    content TEXT NOT NULL,
    priority INTEGER DEFAULT 0,
    active INTEGER DEFAULT 1,
    created_at INTEGER
);

-- Structured data
CREATE TABLE structured_data (
    id INTEGER PRIMARY KEY,
    data_type TEXT NOT NULL,    -- model|view|behavior|schema
    name TEXT UNIQUE NOT NULL,
    scope_path TEXT,
    content TEXT NOT NULL,
    version TEXT DEFAULT '1.0',
    tags TEXT,
    created_at INTEGER,
    updated_at INTEGER
);

-- Abbreviations
CREATE TABLE abbreviations (
    id INTEGER PRIMARY KEY,
    short_form TEXT NOT NULL,
    full_form TEXT NOT NULL,
    domain TEXT,
    scope_path TEXT,
    frequency INTEGER DEFAULT 0,
    created_at INTEGER,
    UNIQUE(short_form, domain)
);

-- Stored procedures
CREATE TABLE stored_procedures (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    db_type TEXT,
    database_name TEXT,
    content TEXT NOT NULL,
    context TEXT,
    parameters TEXT,        -- JSON [{name,type,direction}]
    return_type TEXT,
    tables_used TEXT,       -- JSON array
    sp_dependencies TEXT,   -- JSON array
    scope_path TEXT,
    tags TEXT,
    version INTEGER DEFAULT 1,
    created_at INTEGER,
    updated_at INTEGER,
    UNIQUE(name, database_name)
);

-- SP version history
CREATE TABLE sp_versions (
    id INTEGER PRIMARY KEY,
    sp_id INTEGER REFERENCES stored_procedures(id),
    version INTEGER,
    content TEXT,
    change_note TEXT,
    created_at INTEGER
);
```

---

## Scoring Formula (ICM)

```
score = BM25(query, topic+content+keywords)
      × exp(-0.01 × hours_since_last_used)
      × log(1 + frequency)
      × importance_mult[importance]

importance_mult = {0: 0.5, 1: 1.0, 2: 1.5, 3: 2.0}
BM25: k1=1.5, b=0.75, smoothed IDF
```

---

## RTK Filter Strategies

| Command Pattern | Strategy |
|---|---|
| git log/diff/status/show | Keep changed lines + 3-line context |
| cargo/cmake/make/dotnet build | Errors + warnings only |
| test (cargo test, npm test) | Failures + summary line |
| grep/rg | Matches grouped by file, max 200 lines |
| default | First 50 + last 20 lines |

---

## Phase List

| Phase | Komponen | Sesi |
|---|---|---|
| 01 | Core Foundation (db, config, registry, hooks, migrator) | 1 |
| 02 | ICM Memory + BM25 Scorer | 1 |
| 03 | Graph CRUD + Scanner + Language Extractors | 2 |
| 04 | File Watcher (cross-platform daemon) | 1 |
| 05 | RTK Filter + Runner | 1 |
| 06 | Per-folder Rules + Inheritance | 1 |
| 07 | Structured Data (model/view/behavior/schema) | 1 |
| 08 | Multi-project Registry | 1 |
| 09 | Abbreviation Engine | 0.5 |
| 10 | Stored Procedure Engine + SQL Parser | 1 |
| 11 | Import (ICM/Graphify/JSON/CSV) + Export | 1 |
| 12 | Visual Graph (HTML + Cytoscape.js) | 1.5 |
| 13 | MCP Server (stdio transport) | 1 |
| 14 | Integration Testing + Bug Fix | 1.5 |

**Total: ~15-16 sesi**

---

## Extensibility Points

- `ICMG_REGISTER_EXTRACTOR(lang, Class)` — tambah language extractor
- `ICMG_REGISTER_FILTER(pattern, Class)` — tambah RTK filter
- `ICMG_REGISTER_IMPORTER(name, Class)` — tambah import adapter
- `ICMG_REGISTER_COMMAND(name, Class)` — tambah CLI subcommand
- `ICMG_REGISTER_MCP_TOOL(name, Class)` — tambah MCP tool
- `ICMG_REGISTER_HOOK(event, Class, priority)` — tambah hook
- `migrations/NNNN_*.sql` — schema evolution
- `~/.icmg/extractors/*.py` — external script extractors
