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
| 06 | Per-folder Rules + Inheritance | [x] | 2026-05-06 | 2026-05-06 | — | |
| 07 | Structured Data (model/view/behavior/schema) | [ ] | — | — | — | Depends on: 01, 02 |
| 08 | Multi-project Registry | [ ] | — | — | — | Depends on: 01 |
| 09 | Abbreviation Engine | [ ] | — | — | — | Depends on: 01, 02 |
| 10 | Stored Procedure Engine + SQL Parser | [ ] | — | — | — | Depends on: 01, 03 |
| 11 | Import (ICM/Graphify/JSON/CSV) + Export | [ ] | — | — | — | Depends on: 02,03,09,10 |
| 12 | Visual Graph (HTML + Cytoscape.js) | [ ] | — | — | — | Depends on: 03 |
| 13 | MCP Server (stdio transport) | [ ] | — | — | — | Depends on: 01-10 |
| 14 | Integration Testing + Bug Fix | [ ] | — | — | — | Depends on: semua |

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
