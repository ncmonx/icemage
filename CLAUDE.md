# icm-graph

<!-- AUTO-MANAGED: project-description -->
## Project Overview

**icmg** — single C++ binary unifying ICM (memory), Graphify (knowledge graph), and RTK (command filter).

Additional features: per-folder rules, structured data, multi-project registry, abbreviation engine, stored procedure store, visual graph, MCP server.

Design document: `@docs/plans/2026-05-06-icmg-design.md`
<!-- END AUTO-MANAGED -->

<!-- AUTO-MANAGED: architecture -->
## Architecture

Components: Registry, Hook Bus, Migrator, Config Engine
Subsystems: ICM, Graph, RTK, Rules, Stored Procedures, Abbreviations
Storage: SQLite WAL mode — per-project `.db` files

**Storage layout:**
- `~/.icmg/global.db` — project registry + global memory
- `~/.icmg/config.json` — feature flags + extractor config
- `<project_root>/.icmg/data.db` — per-project data
- `<project_root>/.icmg/watcher.pid` — daemon PID

**Schema tables (per-project data.db):**
`memory_nodes`, `commands`, `graph_nodes`, `graph_edges`, `rules`, `structured_data`, `abbreviations`, `stored_procedures`, `sp_versions`

**Extensibility macros:**
- `ICMG_REGISTER_EXTRACTOR(lang, Class)` — language extractors
- `ICMG_REGISTER_FILTER(pattern, Class)` — RTK filters
- `ICMG_REGISTER_IMPORTER(name, Class)` — import adapters
- `ICMG_REGISTER_COMMAND(name, Class)` — CLI subcommands
- `ICMG_REGISTER_MCP_TOOL(name, Class)` — MCP tools
- `ICMG_REGISTER_HOOK(event, Class, priority)` — hooks
- `migrations/NNNN_*.sql` — schema evolution
- `~/.icmg/extractors/*.py` — external script extractors
<!-- END AUTO-MANAGED -->

<!-- AUTO-MANAGED: patterns -->
## Key Design Decisions

**ICM scoring formula:**
```
score = BM25(query, topic+content+keywords)
      × exp(-0.01 × hours_since_last_used)
      × log(1 + frequency)
      × importance_mult[importance]

importance_mult = {0: 0.5, 1: 1.0, 2: 1.5, 3: 2.0}
BM25: k1=1.5, b=0.75, smoothed IDF
```

**RTK filter strategies:**
- `git log/diff/status/show` → changed lines + 3-line context
- `cargo/cmake/make/dotnet build` → errors + warnings only
- `cargo test / npm test` → failures + summary line
- `grep/rg` → matches grouped by file, max 200 lines
- default → first 50 + last 20 lines
<!-- END AUTO-MANAGED -->

<!-- AUTO-MANAGED: git-insights -->
## Phase Plan

14 phases, ~15-16 sessions total:

| Phase | Component |
|-------|-----------|
| 01 | Core Foundation (db, config, registry, hooks, migrator) |
| 02 | ICM Memory + BM25 Scorer |
| 03 | Graph CRUD + Scanner + Language Extractors |
| 04 | File Watcher (cross-platform daemon) |
| 05 | RTK Filter + Runner |
| 06 | Per-folder Rules + Inheritance |
| 07 | Structured Data (model/view/behavior/schema) |
| 08 | Multi-project Registry |
| 09 | Abbreviation Engine |
| 10 | Stored Procedure Engine + SQL Parser |
| 11 | Import (ICM/Graphify/JSON/CSV) + Export |
| 12 | Visual Graph (HTML + Cytoscape.js) |
| 13 | MCP Server (stdio transport) |
| 14 | Integration Testing + Bug Fix |
<!-- END AUTO-MANAGED -->
