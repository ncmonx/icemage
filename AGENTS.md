# AGENTS.md — icmg for AI Coding Agents

Universal context for **Claude Code, Cursor, Copilot, Aider, Continue, GitHub Codespaces, etc.** This file tells your agent how to use icmg to cut tokens 70-85% without losing context.

> **TL;DR:** Before reading large files or running noisy commands, ask icmg first. Treat it as your code-aware retrieval layer.

---

## When to Use icmg (decision tree)

| Situation | Tool | Why |
|---|---|---|
| Task starts, need orientation | `icmg pack "<task>"` | One 4KB bundle replaces 5-10 explorations |
| Need a specific file's context | `icmg context <file>` | File + symbols + neighbors + memory in 1 call |
| Need a specific function | `icmg graph symbol <Name>` | 30-line symbol node, not 800-line file |
| Want to know who calls X | `icmg graph callers <Name>` | Direct call edges |
| Want to know what X depends on | `icmg graph callees <Name>` | Reverse direction |
| Want full impact radius | `icmg graph reverse-impact <Name> --depth 5` | Transitive — who breaks |
| Errored out — déjà vu? | `icmg explain "<error>"` | Past resolution match |
| Big git diff to review | `icmg diff-summary --ref HEAD~5` | Symbol-grouped, not raw |
| Run noisy command | `icmg run <cmd>` | Filtered output |
| Big SQL query result | `icmg run sqlcmd ...` / `mysql ...` | DB filter: header + 20 rows + footer |
| Run multiple cmds in parallel | `icmg parallel --task "..." --task "..."` | Concurrent (cap=cpu_count, max 32) |
| Pipe existing cmd output | `<cmd> \| icmg filter <type>` | Filter w/o icmg run wrapper |
| Cross-project recall | `icmg recall "X" --all-projects` | Iterates registered projects |
| Paraphrase recall (fuzzy intent) | `icmg recall "X" --semantic` | Hybrid BM25+vec; finds "auth issue" → "fix login bug" |
| Delegate sub-task to LLM | `icmg agent "<task>"` | Pack-bundle + LLM CLI + auto-store decision |
| Interactive multi-turn | `icmg chat` | REPL: each turn packs context + agent + memorize |
| Long-form context (post-mortem, ADR) | `icmg memoir add --title T --content-file F` | Stored never-truncated, importance=2 |
| Browse stored memoirs | `icmg memoir list / show <id> / search <q>` | Cross-linkable via `link` |
| Generate static knowledge site | `icmg wiki build [--include-memoirs]` | Markdown + HTML, no JS deps |
| Stale memory hygiene | `icmg memory decay --threshold-days 30` | Reduces importance; preserves pinned (3) |
| Bootstrap project for AI agents | `icmg init` | Installs hooks + AGENTS.md routing block |
| List dir token-friendly | `icmg ls [path] [--tree / --ext / --json]` | Native, dirs first, sizes formatted |
| Token budget report | `icmg budget` / `icmg budget --html` | Per-tool savings + HTML chart |
| Outline of large file | `icmg summarize <file>` | Heuristic outline + symbol tree |
| Save state mid-task | `icmg session save <name>` | Resume after `/clear` |
| Recall old decision/error | `icmg recall "<query>" [--zone Z]` | BM25 semantic memory |

---

## Workflow Patterns

### Starting a new task

```bash
# 1. Get task-context bundle (4KB, all relevant memory + symbols)
icmg pack "fix EnsureStockRegistered NullRef" --zone sync

# 2. If specific file mentioned, get focused context
icmg context src/Sync/SyncOrder.cs --max-bytes 3000

# 3. Check if past resolution exists
icmg explain "EnsureStockRegistered NullRef"
```

### Before refactoring

```bash
# Find all call sites — direct + transitive
icmg graph callers ProcessOrder
icmg graph reverse-impact ProcessOrder --depth 5
```

### After fixing a bug — preserve knowledge

```bash
icmg known-issue add "Pattern of recurring error" \
    --fix "Concrete fix description with file:line" \
    --zone <subsystem>
```

### Verification gate before commit

```bash
icmg verify --command "ctest" --phase 18
icmg verify --command "cmake --build build" --phase 18
icmg verify gate --phase 18  # exit 0 = all good, exit 1 = block
```

### End-of-session checkpoint

```bash
icmg session save "auth-refactor-step-2"
icmg wflog save \
    --goal "Refactor auth middleware" \
    --decisions "Use JWT not session cookies" \
    --rejected "OAuth proxy (overkill for internal API)" \
    --open "Token expiry policy"
```

---

## Token-Saving Substitutions

When you would naturally reach for these tools, prefer icmg:

| Default reflex | Better via icmg |
|---|---|
| `Read large_file.cs` | `icmg context large_file.cs` |
| `Grep symbol_name` (across repo) | `icmg graph symbol symbol_name` |
| `git diff` (verbose) | `icmg diff-summary` |
| Explore imports manually | `icmg context <file>` (already includes them) |
| Search docs/notes | `icmg recall "<query>"` |
| Run `npm test` (1000-line output) | `icmg run npm test` (failures only) |
| Grep for past similar bugs | `icmg explain "<error>"` |

---

## Project-Aware Conventions

### Reserved memory topic prefixes

Don't collide with these prefixes when storing memory:

| Prefix | Owned by |
|---|---|
| `errors-resolved` | `icmg known-issue` |
| `log-saved` | `icmg wflog` |
| `session-snapshot` | `icmg session` |
| `graph` | `icmg graph scan` (file context auto-sync) |
| `decisions-<project>` | brainstorming convention |
| `preferences` | user-corrections convention |

When user says "remember X", default to `topic="context-<project>"` or `decisions-<project>` unless one of above clearly applies.

### Zone names

Auto-detected from path globs (defaults: api, sync, ui, cli, mcp, rtk, memory, graph, rules, sp, import, data, viz, core, abbreviation, tests, schema, docs). User can `icmg zone add <name> <glob>` to extend. Always pass `--zone <name>` to recall when target is known to scope BM25.

---

## MCP Tools (when configured)

If `.claude/mcp.json` includes icmg, these tools are available:

| Tool | Use when |
|---|---|
| `icmg_recall` | Need past memory hits |
| `icmg_store` | Persist decision/error/preference |
| `icmg_graph_context` | File-level context |
| `icmg_graph_related` | Neighbor files |
| `icmg_rule_apply` | Per-folder coding rules |
| `icmg_sp_search` / `icmg_sp_context` / `icmg_sp_deps` | SQL stored procedures |
| `icmg_abbr_expand` / `icmg_abbr_list` | User abbreviations |
| `icmg_cmd_suggest` | Recall past commands |
| `icmg_project_switch` | Cross-project queries |
| `icmg_stats` | Project health |

Prefer MCP tools over shell `icmg` invocations when available — same data, no subprocess overhead.

---

## Anti-Patterns (avoid)

- **Reading large files without `icmg context` first.** You're burning tokens that icmg already indexed.
- **Repeating searches across `/clear` boundaries.** Use `icmg session save` then `restore`.
- **Storing 50 trivial notes.** icmg has importance levels — only `high`/`critical` survive long.
- **Bypassing zones.** A no-zone recall scans everything; with zones it scopes 5-10× faster.
- **Running raw `git diff` on big PRs.** Use `icmg diff-summary` first, fall back to `--full` only if needed.
- **Running raw `sqlcmd`/`mysql`/`psql`** without `icmg run` — output not filtered, 10K-row dumps blow up context.
- **Sequential `ctest && lint && typecheck`** when they're independent. Use `icmg parallel`.
- **Ignoring `icmg explain` after an error.** Past fixes are usually 1 query away.

---

## Storage / Privacy

- All data stored in `<project_root>/.icmg/data.db` (SQLite WAL).
- Cross-project registry in `~/.icmg/global.db`.
- `.icmg/` is gitignored by default (project-local, not shared).
- No network calls, no telemetry. Pure local SQLite.

---

## Versioning

- Current: `icmg --version` shows installed version.
- Phases shipped (token-efficiency track): 17 (zones), 18 (symbol nodes), 19 (context bundles), 22 (workflow integration).
- Migrations are forward-only — `icmg doctor` reports schema version.

---

## Further Reading

- `README.md` — full command reference
- `CLAUDE.md` — Claude Code-specific context
- `PROGRESS.md` — phase tracker + commit hashes
- `docs/plans/*.md` — design docs per phase

**One-line reminder:** if you're about to read more than 200 lines or run a verbose command, stop — there's an icmg shortcut.
