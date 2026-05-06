# icmg — Phase Progress Tracker

> Update status setiap kali phase selesai atau dimulai.
> Format tanggal: YYYY-MM-DD

---

## Status Legend

| Symbol | Status |
|--------|--------|
| `[ ]` | Pending — belum dikerjakan |
| `[~]` | In Progress — sedang dikerjakan |
| `[x]` | Done — selesai + committed |
| `[!]` | Blocked — ada blocker |

---

## Phase Checklist

| # | Phase | Status | Mulai | Selesai | Commit | Catatan |
|---|-------|--------|-------|---------|--------|---------|
| 01 | Core Foundation (db, config, registry, hooks, migrator, CLI) | [x] | 2026-05-06 | 2026-05-06 | 0f40a43 | |
| 02 | ICM Memory + BM25 Scorer | [x] | 2026-05-06 | 2026-05-06 | 360420d | |
| 03 | Graph CRUD + Scanner + Language Extractors | [x] | 2026-05-06 | 2026-05-06 | c4564ee | C#+PHP added (051e2f9) |
| 04 | File Watcher (cross-platform daemon) | [x] | 2026-05-06 | 2026-05-06 | bbddc9b | |
| 05 | RTK Filter + Runner | [x] | 2026-05-06 | 2026-05-06 | 1780b30 | |
| 06 | Per-folder Rules + Inheritance | [x] | 2026-05-06 | 2026-05-06 | dbc005c | |
| 07 | Structured Data (model/view/behavior/schema) | [x] | 2026-05-06 | 2026-05-06 | 960b648 | |
| 08 | Multi-project Registry | [x] | 2026-05-06 | 2026-05-06 | fe7f150 | |
| 09 | Abbreviation Engine | [x] | 2026-05-06 | 2026-05-06 | f3746d4 | |
| 10 | Stored Procedure Engine + SQL Parser | [x] | 2026-05-06 | 2026-05-06 | d9fb7da | |
| 11 | Import (ICM/Graphify/JSON/CSV) + Export | [x] | 2026-05-06 | 2026-05-06 | 585145b | |
| 12 | Visual Graph (HTML + Cytoscape.js) | [x] | 2026-05-06 | 2026-05-06 | 945fc98 | |
| 13 | MCP Server (stdio transport) | [x] | 2026-05-06 | 2026-05-06 | 1b440f8 | 14 tools, stdio JSON-RPC 2.0, audit log |
| 14 | Integration Testing + Bug Fix | [x] | 2026-05-06 | 2026-05-06 | 6afe58a | 7 suites (ICM/Graph/RTK/Features/MCP/Security/Perf), 14 unit tests, 44 integration tests — all pass |

---

## Dependency Graph

```
01 Core Foundation
├── 02 ICM Memory
│   ├── 07 Structured Data
│   └── 09 Abbreviation
│       └── 11 Import/Export
├── 03 Graph + Scanner
│   ├── 04 File Watcher
│   ├── 10 Stored Procedures
│   │   └── 11 Import/Export
│   └── 12 Visual Graph
├── 05 RTK Filter
├── 06 Rules
└── 08 Multi-project
    └── 11 Import/Export
13 MCP Server ← semua phase 01-10
14 Testing ← semua phase
```

---

## Session Log

Catat setiap sesi kerja di sini.

| Tanggal | Sesi | Phase | Yang Dikerjakan | Hasil |
|---------|------|-------|-----------------|-------|
| 2026-05-06 | 0 | — | Design, review, phase files, git init | 15 plan files dibuat, review 91 issues |
| 2026-05-06 | 1 | 01 | Core Foundation implementation | CMakeLists, db/config/registry/hook_bus/migrator/logger/dispatcher, schema SQL, sqlite3+nlohmann download, commit 0f40a43 |
| 2026-05-06 | 2 | 02 | ICM Memory + BM25 Scorer | MemoryNode/Store/Scorer, store/recall/forget/restore CLI, PRE_STORE abbr hook, scorer invalidate hook, 360420d |
| 2026-05-06 | 3 | 03 | Graph CRUD + Scanner + 9 extractors | GraphNode/Store/Scanner, C++/Python/JS/Go/Rust/Java/C#/PHP/Generic extractors, graph-* CLI, migration 0002, c4564ee+051e2f9 |
| 2026-05-06 | 4 | 04 | File Watcher daemon | Win/Linux/Mac watchers, Debouncer, Daemon fork/PID, graph-watch/stop/watch-status CLI, bbddc9b |
| 2026-05-06 | 5 | 05 | RTK Filter + Runner | Detector, BaseFilter, git/build/test/search/default/package filters, runner (parseArgv+safeExec), RTK class, run+cmd CLI, migration 0003 |
| 2026-05-06 | 6 | 06 | Per-folder Rules + Inheritance | Rule/RuleStore/RuleResolver, rule CLI, PRE_STORE hook, migration 0004, 12 tests |
| 2026-05-06 | 7 | 07 | Structured Data | StructuredData/DataStore (CRUD+versioning+search+revert), data CLI, graph-context integration, 11 tests |
| 2026-05-06 | 8 | 08 | Multi-project Registry | GlobalDb+migrations, ProjectContext, --project flag dispatcher, project CLI, 5 tests |
| 2026-05-06 | 9 | 09 | Abbreviation Engine | AbbrStore CRUD+expand+priority, PRE_STORE auto-detect, PRE_RECALL expand, abbr CLI, 9 tests. Fixed GCC compat (raw strings, TEST macro, whole-archive) |
| 2026-05-06 | 10 | 10 | Stored Procedure Engine | SqlParser A6 state-machine, SpStore CRUD+history+search, sp CLI (add/show/deps/lint/diff/template/impact-table), 15 tests |
| 2026-05-06 | 11 | 11 | Import/Export | BaseImporter (txn+A3+A4+A7), IcmImporter, GraphifyImporter, JsonImporter (A5), CsvImporter, StreamingExporter (A6), import+export CLI, 9 tests, 585145b |
| 2026-05-06 | 12 | 12 | Visual Graph | GraphSerializer+BFS community, HTML/Cytoscape.js+search+filters+theme, DOT/GEXF/GraphML exporters, browser opener, viz CLI, 13 tests, 945fc98 |
| 2026-05-06 | 13 | 13 | MCP Server | McpServer stdio JSON-RPC 2.0, 14 tools (recall/store/graph_context/graph_related/rule_apply/data_get/abbr_expand/abbr_list/sp_search/sp_context/sp_deps/cmd_suggest/project_switch/stats), audit log, .claude/mcp.json, 13 tests |
| 2026-05-06 | 14 | 14 | Integration Testing + Bug Fix | 7 integration suites (44 tests total), 14 unit tests — all pass. Bugs fixed: embedded migrations (binary works from any CWD), compound command routing (graph scan→graph-scan), unresolved graph edges skip, rule path normalization, nested txn in migrator |

---

## Known Issues / Blockers

> Catat blocker yang ditemukan saat implementasi.

| Phase | Issue | Severity | Status |
|-------|-------|----------|--------|
| — | — | — | — |

---

## Review Findings Summary

Total temuan dari security & architecture review (2026-05-06):

| Severity | Count | Status |
|----------|-------|--------|
| CRITICAL | 6 | Incorporated ke phase files |
| HIGH | 18 | Incorporated ke phase files |
| MEDIUM | 15 | Incorporated ke phase files |
| LOW | 11 | Documented sebagai non-goals / future |
| **Missing Features** | **46** | 35 incorporated, 11 → Phase 15-16 |

---

## Planned Future Phases

| # | Phase | Status |
|---|-------|--------|
| 15 | Shell completions, REPL, unified search, memory consolidation, git SHA tagging, token analytics | Planned |
| 16 | Remote sync (S3/git), VS Code extension | Future |
