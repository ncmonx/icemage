# icmg Command Reference

Complete alphabetical reference. For grouped/topical view, see `README.md`.

> Run `icmg --help` for live list. Subcommand help: `icmg <cmd>` (no args) or `icmg <cmd> --help`.

---

## abbr — Abbreviation engine

```
icmg abbr add <abbr> <expansion>     # register
icmg abbr list                       # show all
icmg abbr expand <text>              # expand abbrs in text
icmg abbr remove <abbr>              # remove
```

---

## cmd — Command-frequency tracking

```
icmg cmd suggest [<prefix>] [--limit N] [--json]   # rank by score
icmg cmd record  <command...>                      # manual record
icmg cmd list    [--limit N] [--json]              # all recorded
icmg cmd stats                                      # token-savings analytics
```

---

## context — File context bundle (Phase 19)

```
icmg context <file>
    [--depth N]         # neighbor depth (default 1)
    [--no-symbols]      # skip child symbol list
    [--no-memory]       # skip related memory
    [--max-bytes N]     # cap output (default 4096)
    [--json]
```

Returns: file metadata, imports, used-by, child symbols, top-3 related memory.

---

## data — Structured data (model/view/behavior/schema)

```
icmg data add --kind <K> --name <N> --content <C>   # add entry
icmg data show <id>                                  # detail
icmg data search <query>                             # BM25 search
icmg data revert <id> --version N                    # revert to earlier version
```

---

## design — Design doc gate (Phase 22)

```
icmg design register <feature> <doc_path>   # register draft
icmg design approve  <feature> [--by NAME]  # mark approved
icmg design check    <feature>              # exit 0 if approved (gate)
icmg design list                            # all + status
```

---

## diff-summary — Symbol-aware git diff (Phase 19)

```
icmg diff-summary [--ref REF] [--full]
```

Per-file enclosing-symbol mapping. `--full` appends raw diff.

---

## doctor — Health check

```
icmg doctor    # DB integrity + schema version + config sanity
```

---

## explain — Past-resolution match (Phase 19)

```
icmg explain <error-text>
```

Token-overlap match against `errors-resolved` memory. Returns top 3.

---

## export — Data export

```
icmg export [--format json|csv]
```

---

## forget — Soft-delete memory node

```
icmg forget <id> [--yes]
```

---

## graph — Knowledge graph

```
icmg graph scan [path] [--depth N] [--lang L1,L2] [--json]
icmg graph update [path]
icmg graph context <file> [--json]
icmg graph related <file>
icmg graph impact <file>
icmg graph transitive-impact <symbol|file> [--depth N] [--types t1,t2]
icmg graph reverse-impact   <symbol|file> [--depth N] [--types t1,t2]
icmg graph symbol  <name>                      # find by symbol_name
icmg graph callers <symbol>                    # incoming calls edges
icmg graph callees <symbol>                    # outgoing calls edges
icmg graph search  <query>
icmg graph list    [--lang L] [--zone Z] [--kind K]   # default kind=file
icmg graph stats
icmg graph orphans
icmg graph cycles
icmg graph hot
icmg graph watch [dir]
icmg graph stop  [dir]
icmg graph watch-status
```

**Edge types:** imports, uses, companion, calls, extends, implements.
**Node kinds:** file, class, interface, struct, record, method, sp.

---

## import — Import adapters

```
icmg import icm     <path>     # ICM dump
icmg import kgraph  <path>     # Knowledge-graph JSON
icmg import graphify <path>    # alias for kgraph
icmg import json    <path>
icmg import csv     <path>
```

---

## known-issue — Recurring error registry (Phase 22)

```
icmg known-issue add   <pattern> --fix <description> [--zone Z]
icmg known-issue match <error-text>
icmg known-issue list  [--zone Z] [--limit N]
icmg known-issue stats
```

Backed by `memory_nodes` topic prefix `errors-resolved`.

---

## memory — Memory umbrella

```
icmg memory list    [--topic PREFIX] [--limit N] [--json]
icmg memory show    <id>
icmg memory search  <query>                # alias for `recall`
icmg memory stats                          # importance breakdown + topic buckets
icmg memory history [--limit N]            # recent recall queries
icmg memory forget  <id>                   # delegates to `forget`
icmg memory restore <id>                   # delegates to `restore`
icmg memory purge   [--days N]             # hard-delete soft-deleted
```

---

## pack — Task-context bundle (Phase 19)

```
icmg pack <task description...>
    [--zone Z]          # scope memory to zone
    [--memory-limit N]  # default 5
    [--max-bytes N]     # default 4096
```

Returns markdown bundle: relevant memory + matching symbols.

---

## phase — Phase lifecycle (Phase 22)

```
icmg phase list
icmg phase show  <num>
icmg phase start <num> [--name X] [--goal Y] [--plan PATH]
icmg phase verify <num>                  # GO/NO-GO
icmg phase ship  <num> [--commit SHA]
```

---

## project — Multi-project registry

```
icmg project register <name> <path>     # register
icmg project list
icmg project switch <name>
icmg --project <name> <command>          # one-off override
```

---

## recall — BM25 memory recall

```
icmg recall <query>
    [--limit N]            # default 10
    [--topic PREFIX]       # filter by topic prefix
    [--zone Z]             # restrict corpus to zone
    [--fuzzy]              # fuzzy fallback
    [--explain]            # show score breakdown
    [--history]            # show recent queries instead
    [--json]
```

---

## restore — Restore soft-deleted node

```
icmg restore <id>
```

---

## rule — Per-folder rules

```
icmg rule add --type T --name N --content C [--scope-path P] [--priority N] [--active 0|1]
icmg rule list [--scope-path P]
icmg rule for <path>             # resolved set at this path
icmg rule remove <id>
```

---

## run — Tkil command filter

```
icmg run [options] <command...>

  --raw         # no filtering
  --json        # JSON output with metadata
  --dry-run     # show detected filter without running
  --stream      # streaming filter
  --timeout N   # exec timeout in ms (default 60000)
```

**Filter strategies:**

| Pattern | Strategy |
|---|---|
| `git log/diff/show` | changed lines + 3-line context |
| `git diff --stat / --name-only / --shortstat / etc.` | pass-through |
| `git status` | pass-through |
| `cargo/cmake/make/dotnet build` | errors + warnings only |
| `cargo test / npm test` | failures + summary |
| `grep/rg` | matches grouped by file, max 200 lines |
| default | first 50 + last 20 lines |

---

## session — Snapshot context (Phase 19)

```
icmg session save    <name>
icmg session restore <name>
icmg session list
```

Backed by topic prefix `session-snapshot`.

---

## sp — Stored procedures

```
icmg sp add <file>
icmg sp show <name>
icmg sp deps <name>                # dependencies
icmg sp lint <file>                # SQL lint
icmg sp diff <name>                # version diffs
icmg sp template <kind>            # SP template
icmg sp impact-table <table>       # SPs touching table
icmg sp search <query>
```

---

## stats — Project statistics

```
icmg stats
```

Output: memory nodes, graph nodes, edges, rules, abbreviations, stored procs, commands seen.

---

## store — Store memory node

```
icmg store <topic> <content>
    [--importance LEVEL]    # low|med|high|crit (default med)
    [--keywords K1,K2]      # comma-separated
    [--zone Z]              # default 'default'
    [--ttl SECONDS]         # expires_at
    [--force]               # bypass dedup (Jaccard >= 0.85)
```

---

## verify — Verification audit trail (Phase 22)

```
icmg verify --command "<cmd>" [--phase N]    # run + record
icmg verify show [--phase N]                 # list recorded
icmg verify gate [--phase N]                 # exit 0 if all pass
```

Records: exit code, output hash (FNV1a-64), first 1KB of output, duration.

---

## viz — Visual graph (HTML)

```
icmg viz [options]

  --output FILE             # default icmg-viz/index.html
  --format html|dot|gexf|graphml
  --no-open                 # don't auto-open browser
  --filter-lang L1,L2
  --zone <name>
  --community <id>
  --estimate                # output size only
```

**HTML interactive features:**
- fcose layout (default), CoSE/BFS/concentric/grid/circle alternatives
- Label truncation 22 chars + hover tooltip
- Distinct edge colors per type (imports/uses/companion/calls/...)
- Hide-binary toggle (default on — strips PNG/ICO/DLL)
- Hide-orphans toggle
- Group-by-directory toggle (compound parent nodes)
- Search with zoom-to-fit on ≤8 matches
- Neighbor navigation panel (click neighbor → focus + show its info)

---

## wflog — Queryable session log (Phase 22)

```
icmg wflog save --goal G --decisions D [--rejected R] [--open O] [--zone Z]
icmg wflog search <query>
icmg wflog recent [--limit N]
icmg wflog show <id>
```

Topic prefix: `log-saved`.

---

## zone — Zone management (Phase 17)

```
icmg zone list [--json]
icmg zone show <name> [--limit N]
icmg zone add <name> <glob>            # e.g. add payment "src/payment/**"
icmg zone rm <name>
icmg zone assign <glob> <zone>         # bulk re-tag
icmg zone rebuild                      # apply rules to all nodes
icmg zone resolve <path>               # debug
```

**Glob syntax:** `prefix/**`, `*.ext`, or literal substring.

---

## Global Flags

| Flag | Effect |
|---|---|
| `--verbose, -v` | Verbose output |
| `--version` | Show version |
| `--help, -h` | Show help |
| `--project <name>` | Override project context for this invocation |
| `--mcp-server` | Run as MCP stdio server (not a regular subcommand) |

---

## Exit Codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | User error (missing arg, invalid input) |
| 2 | Gate-style "no records to check" (e.g., `verify gate` with empty table) |
| Other | Underlying command exit (e.g., `icmg run` propagates child exit) |

---

## See Also

- `README.md` — overview + token-efficiency rationale
- `AGENTS.md` — usage guide for AI coding agents
- `CLAUDE.md` — Claude Code-specific project context
- `PROGRESS.md` — phase tracker
- `docs/plans/*.md` — per-phase implementation plans
