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
| 01 | Core Foundation (db, config, registry, hooks, migrator, CLI) | [ ] | — | — | — | |
| 02 | ICM Memory + BM25 Scorer | [ ] | — | — | — | Depends on: 01 |
| 03 | Graph CRUD + Scanner + Language Extractors | [ ] | — | — | — | Depends on: 01 |
| 04 | File Watcher (cross-platform daemon) | [ ] | — | — | — | Depends on: 03 |
| 05 | RTK Filter + Runner | [ ] | — | — | — | Depends on: 01 |
| 06 | Per-folder Rules + Inheritance | [ ] | — | — | — | Depends on: 01 |
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
