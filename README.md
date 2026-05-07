# icmg — Unified Memory, Knowledge Graph & Token-Saving CLI

[![release](https://img.shields.io/github/v/release/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/releases)
[![tests](https://img.shields.io/badge/tests-17%2F17%20passing-brightgreen)](#)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](#)

**Single C++17 binary** unifying:

- **ICM (memory)** — BM25-ranked persistent memory with importance + recency decay
- **Knowledge Graph** — files + symbols (functions, classes, SPs) + typed edges
- **Tkil** — token-saving command filter (formerly RTK; renamed to avoid trademark collision)
- **Workflow gates** — verify, design, phase lifecycle, known-issue registry
- **Context bundles** — single-call aggregation (`context`, `pack`, `diff-summary`)

> **Mission:** make Claude / Cursor / any AI coding assistant **70-85% more token-efficient** without losing context.

---

## Quick Start

```bash
# Build (Windows/MSYS2/MinGW)
cmake -B build && cmake --build build
# Or download release binary: https://github.com/ncmonx/icm-graph/releases

# First scan
icmg graph scan .

# Recall context for a task
icmg pack "fix login bug" --max-bytes 4096

# Filter noisy build output
icmg run npm test

# Symbol-level call analysis
icmg graph callers ProcessOrder
```

---

## Why icmg?

Claude Code sessions burn 50K+ tokens reading large files, running noisy commands, and re-exploring the same codebase. icmg turns that into a structured, queryable graph + memory that AI agents can drill into precisely.

| Without icmg | With icmg |
|---|---|
| Read 800-line `Core.cs` to find `ProcessOrder` | `icmg graph symbol ProcessOrder` → 30-line method node |
| `git diff HEAD~5` → 500-line dump | `icmg diff-summary --ref HEAD~5` → 20-line symbol-grouped summary |
| 5-10 Read+Grep+Glob to start a task | `icmg pack "task description"` → one 4KB bundle |
| Re-explain context after each `/clear` | `icmg session save mytask` → snapshot, restore later |
| Same error solved twice | `icmg known-issue add/match` → deduplicated past fixes |

---

## Command Reference

### Memory (ICM)

| Command | Purpose |
|---|---|
| `icmg store <topic> <content> [--importance LEVEL] [--zone Z]` | Store a memory node |
| `icmg recall <query> [--zone Z] [--topic PREFIX] [--limit N] [--explain]` | BM25-ranked recall |
| `icmg memory list [--topic P] [--limit N]` | List nodes |
| `icmg memory show <id>` | Full node detail |
| `icmg memory search <query>` | Alias for recall |
| `icmg memory stats` | Importance breakdown + topic buckets |
| `icmg memory history` | Recent recall queries |
| `icmg memory purge [--days N]` | Hard-delete soft-deleted nodes |
| `icmg forget <id>` | Soft-delete |
| `icmg restore <id>` | Restore deleted |

**Importance levels:** `low` (0.5×), `med` (1.0×), `high` (1.5×), `critical` (2.0×).
**Score formula:** `BM25 × exp(-0.01 × hours_since_last_used) × log(2 + frequency) × importance_mult`.

### Knowledge Graph

| Command | Purpose |
|---|---|
| `icmg graph scan [path]` | Scan directory → file + symbol nodes + edges |
| `icmg graph update [path]` | Re-scan (skips unchanged files via hash) |
| `icmg graph context <file> [--depth N]` | Show graph context for a file |
| `icmg graph related <file>` | Show neighboring files |
| `icmg graph impact <file>` | Files impacted by changing this file |
| `icmg graph transitive-impact <symbol\|file> [--depth N] [--types ...]` | Forward closure (what X reaches) |
| `icmg graph reverse-impact <symbol\|file> [--depth N]` | Reverse closure (who breaks if X changes) |
| `icmg graph symbol <name>` | Find symbol by name (any kind) |
| `icmg graph callers <symbol>` | Incoming call edges |
| `icmg graph callees <symbol>` | Outgoing call edges |
| `icmg graph search <query>` | Search nodes by query |
| `icmg graph list [--lang L] [--zone Z] [--kind K]` | List nodes (default kind=file) |
| `icmg graph stats` | Node + edge counts |
| `icmg graph orphans` | Files with no inbound edges |
| `icmg graph cycles` | Detect circular dependencies |
| `icmg graph hot` | Most accessed files |
| `icmg graph watch [dir]` | Start file watcher daemon |
| `icmg graph stop [dir]` | Stop watcher |
| `icmg graph watch-status` | Watcher status |

**Edge types:** `imports`, `uses`, `companion`, `calls`, `extends`, `implements`.
**Node kinds:** `file`, `class`, `interface`, `struct`, `record`, `method`, `sp`.
**Languages with symbol-level extraction:** C# (`.cs`), SQL/T-SQL (`.sql`).

### Zones (Subsystem Partitioning)

| Command | Purpose |
|---|---|
| `icmg zone list [--json]` | All zones + node counts |
| `icmg zone show <name> [--limit N]` | Members of a zone |
| `icmg zone add <name> <glob>` | Register path glob → zone |
| `icmg zone rm <name>` | Remove zone definition |
| `icmg zone assign <glob> <zone>` | Bulk re-tag matching nodes |
| `icmg zone rebuild` | Re-tag every node from current rules |
| `icmg zone resolve <path>` | Debug: which zone for this path? |

**Use with:** `icmg recall "X" --zone sync`, `icmg viz --zone api`, `icmg graph list --zone tests`.

### Tkil — Token-Saving Command Filter

| Command | Purpose |
|---|---|
| `icmg run <command...>` | Run command, filter noisy output |
| `icmg run --raw <command>` | No filtering (debug) |
| `icmg run --json <command>` | JSON output with metadata |
| `icmg run --dry-run <command>` | Show detected filter without running |
| `icmg cmd suggest [<prefix>]` | Suggest commands by frequency |
| `icmg cmd record <command>` | Manually record a command |
| `icmg cmd list` | List all recorded commands |
| `icmg cmd stats` | Token-saving stats (avg reduction %, est tokens saved) |

**Filter strategies:**
- `git log/diff/show` → changed lines + 3-line context
- `git diff --stat / --name-only` → passes through (no @@ hunks)
- `git status` → passes through
- `cargo/cmake/make/dotnet build` → errors + warnings only
- `npm test / cargo test` → failures + summary
- `grep/rg` → matches grouped by file, max 200 lines
- default → first 50 + last 20 lines

### Context Bundles (Phase 19)

| Command | Purpose |
|---|---|
| `icmg context <file> [--depth N] [--no-symbols] [--no-memory] [--max-bytes N]` | File bundle: lang/imports/used-by/symbols/memory |
| `icmg pack <task...> [--zone Z] [--max-bytes N] [--memory-limit N]` | Task aggregator: memory + matching symbols |
| `icmg diff-summary [--ref REF] [--full]` | Symbol-aware git diff |
| `icmg explain <error-text>` | Match against `errors-resolved` memory |
| `icmg session save <name>` | Snapshot recent recall queries |
| `icmg session restore <name>` | Re-emit snapshot |
| `icmg session list` | All saved sessions |

**Output cap:** all bundle commands enforce `--max-bytes` (default 4096); overflow spills to `/tmp/icmg-spill-<hash>.txt` with head+tail preserved.

### Workflow Integration (Phase 22)

| Command | Purpose |
|---|---|
| `icmg known-issue add <pattern> --fix <desc> [--zone Z]` | Register past resolution |
| `icmg known-issue match <error-text>` | Find prior fixes |
| `icmg known-issue list [--zone Z] [--limit N]` | All registered |
| `icmg known-issue stats` | Most-recurring errors |
| `icmg verify --command "<cmd>" [--phase N]` | Run + record exit/hash/output |
| `icmg verify show [--phase N]` | List recorded verifications |
| `icmg verify gate [--phase N]` | Exit 0 if all pass — pre-push gate |
| `icmg phase list` | Show all phases + status |
| `icmg phase show <num>` | Full detail |
| `icmg phase start <num> [--name X] [--goal Y] [--plan PATH]` | Mark in-progress |
| `icmg phase verify <num>` | Goal-backward GO/NO-GO check |
| `icmg phase ship <num> [--commit SHA]` | Mark completed |
| `icmg design register <feature> <doc_path>` | Register draft design |
| `icmg design approve <feature> [--by NAME]` | Mark approved |
| `icmg design check <feature>` | Exit 0 if approved (gate) |
| `icmg design list` | All designs + status |
| `icmg wflog save --goal G --decisions D [--rejected R] [--open O]` | Append session log |
| `icmg wflog search <query>` | BM25 over saved entries |
| `icmg wflog recent [--limit N]` | Newest first |
| `icmg wflog show <id>` | Full detail |

### Stored Procedures

| Command | Purpose |
|---|---|
| `icmg sp add <file>` | Add SP from .sql file |
| `icmg sp show <name>` | Full SP body |
| `icmg sp deps <name>` | Show dependencies |
| `icmg sp lint <file>` | Lint SQL syntax |
| `icmg sp diff <name>` | Show version diffs |
| `icmg sp template <kind>` | SP template |
| `icmg sp impact-table <table>` | SPs that touch this table |

### Abbreviations

| Command | Purpose |
|---|---|
| `icmg abbr add <abbr> <expansion>` | Register abbreviation |
| `icmg abbr list` | All abbreviations |
| `icmg abbr expand <text>` | Expand abbrs in text |
| `icmg abbr remove <abbr>` | Remove |

### Per-Folder Rules

| Command | Purpose |
|---|---|
| `icmg rule add --type T --name N --content C [--scope-path P]` | Add rule |
| `icmg rule list [--scope-path P]` | List rules |
| `icmg rule for <path>` | Resolved rules at path |
| `icmg rule remove <id>` | Remove rule |

### Structured Data

| Command | Purpose |
|---|---|
| `icmg data add --kind <model\|view\|behavior\|schema> --name N --content C` | Add structured entry |
| `icmg data show <id>` | Detail |
| `icmg data search <query>` | Search |
| `icmg data revert <id> --version N` | Revert to version |

### Multi-Project Registry

| Command | Purpose |
|---|---|
| `icmg project register <name> <path>` | Register project |
| `icmg project list` | All projects |
| `icmg project switch <name>` | Default project |
| `icmg --project <name> <command>` | One-off cross-project |

### Visual Graph (HTML)

| Command | Purpose |
|---|---|
| `icmg viz [--output FILE] [--zone Z] [--filter-lang L1,L2] [--no-open]` | Generate Cytoscape.js HTML |
| `icmg viz --format dot \| gexf \| graphml` | Alternative export formats |
| `icmg viz --community <id>` | Show one community only |
| `icmg viz --estimate` | Output size estimate |

**HTML features:** fcose layout, label truncation+tooltip, distinct edge colors per type, hide-binary toggle, group-by-directory toggle, search with zoom-to-fit, neighbor navigation panel.

### Import / Export

| Command | Purpose |
|---|---|
| `icmg import icm <path>` | Import from ICM dump |
| `icmg import kgraph <path>` | Import from Graphify/KGraph JSON |
| `icmg import json <path>` | Generic JSON import |
| `icmg import csv <path>` | CSV import |
| `icmg export [--format json\|csv]` | Export all data |

### Health & Stats

| Command | Purpose |
|---|---|
| `icmg stats` | Project-wide stats |
| `icmg doctor` | DB integrity + schema version + config check |

---

## MCP Server

icmg ships an MCP (Model Context Protocol) server so Claude Code (and any MCP-aware client) can call icmg directly. Configure `.claude/mcp.json`:

```json
{
  "mcpServers": {
    "icmg": {
      "command": "icmg",
      "args": ["--mcp-server"]
    }
  }
}
```

**Tools exposed:** `icmg_recall`, `icmg_store`, `icmg_graph_context`, `icmg_graph_related`, `icmg_rule_apply`, `icmg_data_get`, `icmg_abbr_expand`, `icmg_abbr_list`, `icmg_sp_search`, `icmg_sp_context`, `icmg_sp_deps`, `icmg_cmd_suggest`, `icmg_project_switch`, `icmg_stats`.

---

## Storage Layout

```
~/.icmg/
├── global.db              # project registry + global memory
├── config.json            # feature flags + extractor config
└── extractors/*.py        # external script extractors

<project_root>/.icmg/
├── data.db                # per-project SQLite (WAL mode)
└── watcher.pid            # daemon PID
```

**Schema tables:** `memory_nodes`, `commands`, `graph_nodes`, `graph_edges`, `rules`, `structured_data`, `abbreviations`, `stored_procedures`, `sp_versions`, `zone_config`, `verifications`, `phases`, `designs`.

---

## Token-Efficiency Roadmap

Implemented (v0.5.0):

| Phase | Feature | Saving |
|---|---|---|
| 17 | Zone partitioning — scoped recall, sharper IDF | 30-50% on recall |
| 18 | Symbol-level nodes — read fn instead of file | 80%+ on fix-bug-X |
| 19 | Context bundles — `context`/`pack`/`diff-summary` | 50-70% session start |
| 22 | Workflow integration — known-issue, verify, phase, design | Audit + 5-10× context retention |

Planned:

| Phase | Feature | Saving |
|---|---|---|
| 20 | Output compression — auto-summarize big files, output cap, `icmg budget` | 60-80% large reads |
| 21 | Advanced — semantic embeddings, agent proxy, MCP resources, REPL | +20-30% |

**Cumulative target:** 80-90% token reduction + 90% context retention across resets.

---

## Build Instructions

### Linux / macOS

```bash
cmake -B build
cmake --build build -j
cd build && ctest --output-on-failure
sudo cp icmg /usr/local/bin/
```

### Windows (MSYS2 + MinGW)

```bash
export PATH="/c/msys64/mingw64/bin:/c/Program Files/CMake/bin:$PATH"
cmake -B build
cmake --build build
cd build && ctest --output-on-failure
cp icmg.exe /c/msys64/usr/local/bin/
```

**Requirements:** CMake 3.20+, C++17 compiler (gcc 9+/clang 10+/MSVC 2019+). Bundled deps: SQLite3 amalgamation, nlohmann/json single header.

---

## Extensibility

Drop-in registration via static-init macros:

| Macro | Use for |
|---|---|
| `ICMG_REGISTER_EXTRACTOR(lang, Class)` | Language extractors (`src/graph/extractor/*.cpp`) |
| `ICMG_REGISTER_SYMBOL_EXTRACTOR(lang, Class)` | Symbol extractors (Phase 18) |
| `ICMG_REGISTER_FILTER(pattern, Class)` | Tkil filters (`src/tkil/filters/*.cpp`) |
| `ICMG_REGISTER_IMPORTER(name, Class)` | Import adapters |
| `ICMG_REGISTER_COMMAND(name, Class)` | CLI subcommands |
| `ICMG_REGISTER_MCP_TOOL(name, Class)` | MCP tools |
| `ICMG_REGISTER_HOOK(event, Class, priority)` | Hook bus |

Schema evolution: drop `migrations/NNNN_*.sql` and mirror in `src/core/embedded_migrations.hpp`.

---

## License

MIT.

## Documentation

- **Plans:** `docs/plans/*.md` — design + per-phase implementation plans
- **CLAUDE.md** — agent-friendly project context (loaded by Claude Code)
- **AGENTS.md** — universal AI agent guide (Cursor, Copilot, etc.)
- **PROGRESS.md** — phase tracker with commit hashes
- **Releases:** https://github.com/ncmonx/icm-graph/releases

---

**icmg = make AI sessions cheap, fast, and never lose context.**
