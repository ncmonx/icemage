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
## Phase Plan & Status

14 phases, ~15-16 sessions total. All phases currently **Pending** (as of 2026-05-06).

| Phase | Component | Status |
|-------|-----------|--------|
| 01 | Core Foundation (db, config, registry, hooks, migrator) | [ ] |
| 02 | ICM Memory + BM25 Scorer | [ ] |
| 03 | Graph CRUD + Scanner + Language Extractors | [ ] |
| 04 | File Watcher (cross-platform daemon) | [ ] |
| 05 | RTK Filter + Runner | [ ] |
| 06 | Per-folder Rules + Inheritance | [ ] |
| 07 | Structured Data (model/view/behavior/schema) | [ ] |
| 08 | Multi-project Registry | [ ] |
| 09 | Abbreviation Engine | [ ] |
| 10 | Stored Procedure Engine + SQL Parser | [ ] |
| 11 | Import (ICM/Graphify/JSON/CSV) + Export | [ ] |
| 12 | Visual Graph (HTML + Cytoscape.js) | [ ] |
| 13 | MCP Server (stdio transport) — depends on 01-10 | [ ] |
| 14 | Integration Testing + Bug Fix — depends on all | [ ] |

**Planned future phases:**
- Phase 15: shell completions, REPL, unified search, memory consolidation, git SHA tagging, token analytics
- Phase 16: remote sync (S3/git), VS Code extension
- **Phase 17: Zone partitioning** — scope recall/graph/viz to subsystem zones (5-10× faster recall, sharper BM25)
- **Phase 18: Function/symbol-level nodes** — two-tier graph (file + child symbols), body-hash staleness, call edges (80%+ token cut on "fix bug X")
- **Phase 19: Context bundle commands** — `icmg context <file>`, `icmg pack <task>`, `icmg diff-summary`, `icmg explain <error>`, `icmg session save/restore` (50-70% saving on session start)
- **Phase 20: Output compression** — `icmg summarize`, Read-shrink hook, output cap, `icmg budget` token tracker (60-80% on large reads)
- **Phase 21: Advanced** — semantic embeddings, `icmg agent`, MCP resources protocol, `icmg chat` REPL (additional 20-30%)
- **Phase 22: Workflow integration** — KG transitive impact + GSD lifecycle commands (`icmg phase verify` goal-backward) + Superpowers gates (`icmg known-issue/verify/design/log`). Hook templates bundle. Closes the "fast → rigorous" gap.

Cumulative target: ~80-90% token reduction + 90% context retention across resets.
- MVP path: 17 + 19's `context` (5 days, ~50% immediate).
- Rigorous path: 17 → 18 → 22 → rest. Adds audit trail + workflow gates.

**Session log:**
- 2026-05-06 Session 0: Design, review, phase files, git init — 15 plan files created, 91 issues reviewed (CRITICAL 6, HIGH 18, MEDIUM 15, LOW 11, Missing Features 46 → 35 incorporated, 11 deferred to Phase 15-16)

Progress tracker: `@PROGRESS.md`
<!-- END AUTO-MANAGED -->

## Build Instructions (Windows/MSYS2)

```bash
# Must set PATH before cmake — use dangerouslyDisableSandbox=true in Bash tool
export PATH="/c/msys64/mingw64/bin:/c/Program Files/CMake/bin:$PATH"
cmake -B build          # configure (first time or after CMakeLists change)
cmake --build build     # build all
cd build && ctest --output-on-failure  # run unit tests
```

## Coding Conventions

- Registry: `ICMG_REGISTER_*(name, Class)` macros for static-init (new subsystem → new tool file)
- Stores accept `core::Db&` — no global state in subsystems
- SQL: parameterized queries only (`db.run(sql, {params...})`)
- New CLI command: subclass `BaseCommand`, `ICMG_REGISTER_COMMAND`
- New MCP tool: subclass `BaseMcpTool`, `ICMG_REGISTER_MCP_TOOL`
- New DB column: add migration `migrations/NNNN_*.sql`
- Tests: `tests/test_main.hpp` harness with `TEST()` + `ASSERT_*` macros

## DB Schema Key Notes (prevent future bugs)

- `graph_edges` uses `src`/`dst` columns (INTEGER FK) — NOT `src_id`/`dst_id`
- `rules` has `rule_type, name, content, priority, active` — NOT `rule_body`
- `memory_nodes` has NO `updated_at` column
- `stored_procedures` UNIQUE on `(name, database_name)`

## Integration Tests

```bash
# Full suite (from project root, MSYS2 bash)
bash tests/run_all.sh

# Individual suites
bash tests/test_icm.sh && bash tests/test_graph.sh && bash tests/test_rtk.sh
bash tests/test_features.sh && bash tests/test_mcp.sh
bash tests/test_security.sh && bash tests/test_performance.sh
```

## MCP Config

`.claude/mcp.json` configured to run `icmg --mcp-server`. 14 tools available:
`icmg_recall`, `icmg_store`, `icmg_graph_context`, `icmg_graph_related`,
`icmg_rule_apply`, `icmg_data_get`, `icmg_abbr_expand`, `icmg_abbr_list`,
`icmg_sp_search`, `icmg_sp_context`, `icmg_sp_deps`, `icmg_cmd_suggest`,
`icmg_project_switch`, `icmg_stats`
