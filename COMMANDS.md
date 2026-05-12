# icmg Command Reference

Complete alphabetical reference. For grouped/topical view, see `README.md`.

> Run `icmg --help` for live list. Subcommand help: `icmg <cmd>` (no args) or `icmg <cmd> --help`.

---

## agent — LLM proxy (Phase 23)

```
icmg agent "<task>"               # pack→prompt→LLM CLI→auto-store decision
icmg agent --dry-run "<task>"     # show prompt only, no LLM call
icmg agent --no-store "<task>"    # don't auto-memorize result
icmg agent --no-pack  "<task>"    # skip pack step (terse prompt)
icmg agent --command "<cmd>"      # override agent.command
icmg agent --timeout 180          # default 120s
```

Config (`~/.icmg/config.json`): `agent.command` (default `claude --print`), `agent.system_prompt_path`, `agent.max_tokens`.

---

## chat — Interactive REPL (Phase 23)

```
icmg chat                         # default
icmg chat --no-llm                # sandbox: print packed prompt, no LLM call
icmg chat --no-pack               # skip pack each turn
icmg chat --session <name>        # custom session id
```

Slash commands: `\save <name>`, `\load <name>`, `\clear`, `\help`, `\quit`. History persists at `~/.icmg/chat-history.txt`.

---

## embed — Build/refresh semantic index (Phase 23)

```
icmg embed memory [--limit N] [--force]     # embed memory_nodes
icmg embed graph  [--limit N] [--force]     # embed graph_nodes
icmg embed --status                          # availability + counts
```

Requires `pip install optional embedder`. Skips rows whose `body_hash` is unchanged unless `--force`. Without Python sidecar: `--status` reports unavailable; `recall --semantic` falls back to ranking algorithm.

---

## init — Bootstrap project for AI agents (v0.9.2)

```
icmg init [--no-hooks] [--no-agents] [--no-scan] [--force]
```

Writes `.claude/settings.local.json` with PreToolUse:Bash + Read hooks, drops bundled hook scripts to `.claude/hooks/`, appends/updates `<!-- icmg:start -->` block in `AGENTS.md`. Idempotent — re-run to refresh after icmg upgrades.

---

## ls — Token-friendly directory listing (v0.9.1)

```
icmg ls [path] [--all / -a] [--tree] [--json]
                  [--limit N] [--ext E]
```

Native std::filesystem; path-with-spaces handled, dirs first, human-formatted sizes (B/K/M/G/T), auto-truncate at `--limit 100`. Use over `icmg run ls` for routine listings.

---

## memoir — Long-form narrative memory (v0.10.0)

```
icmg memoir add --title T (--content TEXT | --content-file F | stdin)
                [--keywords K] [--zone Z] [--link <other-id>]
icmg memoir list [--limit N] [--zone Z]
icmg memoir show <id>
icmg memoir search <query> [--limit N]
icmg memoir link <id> --to <other-id>
```

Stored as `memory_nodes` with topic prefix `memoir:`. Force-stored (no dedup), importance=2 default. Use for post-mortems, design rationales, customer interview synthesis, anything richer than 1-line `icmg store`.

---

## wiki — Markdown + HTML knowledge site (v0.10.0)

```
icmg wiki build [--out wiki/] [--no-html] [--no-md]
                [--max-files N] [--include-memoirs]
icmg wiki serve [--port N]   # prints HTTP-server hint (static files)
```

Outputs:
- `wiki/index.{md,html}` — file tree + memoir list + stats
- `wiki/files/<path>.{md,html}` — per-file: symbols, language, zone
- `wiki/symbols/<name>.{md,html}` — per-symbol: kind, file link, body excerpt
- `wiki/memoirs/<title>.{md,html}` — long-form (with `--include-memoirs`)
- `wiki/style.css` — single self-contained stylesheet, no JS, no CDN

Cross-linked. Host on github pages directly.

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

## budget — Token-budget tracker (Phase 20+21)

```
icmg budget                       # window summary (last 24h default)
icmg budget --window 7d
icmg budget by-tool [--window N]  # per-tool aggregate
icmg budget by-cmd [--limit N]    # top N commands by tokens saved
icmg budget --html [--out FILE]   # HTML dashboard (T9)
icmg budget record <tool> --raw N [--filtered M] [--cmd "..."]
```

`icmg run` auto-records to tool_invocations; `--html` reads it for charts.

---

## context — File context bundle (Phase 19)

```
icmg context <file>
    [--symbol NAME]     # slice to single symbol body (80%+ token cut vs full file)
    [--depth N]         # neighbor depth (default 1)
    [--no-symbols]      # skip child symbol list
    [--no-memory]       # skip related memory
    [--max-bytes N]     # cap output (default 4096)
    [--json]
```

Returns: file metadata, imports, used-by, child symbols, top-3 related memory.

`--symbol NAME` slices the output to a single function/class body extracted from the knowledge graph, skipping the rest of the file. Substring and case-insensitive match. Use when you need one function, not the whole module — delivers surgical context at a fraction of the token cost.

---

## data — Structured data (model/view/behavior/schema)

```
icmg data add --kind <K> --name <N> --content <C>   # add entry
icmg data show <id>                                  # detail
icmg data search <query>                             # ranking algorithm search
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

## filter — Pipe-style Tkil filter (Phase 21)

```
<cmd> | icmg filter <type>
<cmd> | icmg filter --as "<original-cmd-string>"
```

Types: `git | build | test | search | npm | db | vitest | playwright | tsc | lint | default`. `--as` auto-detects from command string.

Dedicated filters added in v0.10.0:
- `vitest` — keep `❯ FAIL` blocks + stack traces + summary; drop PASS spam
- `playwright` — `✘` failures + screenshot/video paths + summary
- `tsc` — TS<code> error lines + "Found N errors" summary
- `lint` — eslint, clippy, ruff, golangci-lint, dotnet format, prettier, black, flake8, mypy

Examples:
```bash
npm test 2>&1 | icmg filter test
git diff       | icmg filter --as "git diff"
sqlcmd -Q ...  | icmg filter db
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
icmg memory decay   [--threshold-days N]   # reduce importance of stale nodes (v0.10.0)
                    [--floor N] [--dry-run] [--no-pinned-keep]
```

`decay` walks `memory_nodes` where `last_used < now - N*86400` and `importance > floor` and decrements importance by 1. Preserves importance=3 (pinned/critical) by default. Run as cron / pre-push hook to keep top-of-mind decisions promoted.

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

## parallel — Subprocess fan-out (Phase 21)

```
icmg parallel --task "<cmd>" [--task "<cmd>" ...]
              [--max-concurrency N]
              [--timeout-ms N]
              [--fail-fast]
              [--merge json|concat|none]
              [--json]
```

Aggregates exit code = max(child exits). Default merge `json` (parses + flattens arrays). On Windows, full paths with spaces work — uses `cmd.exe /s /c "<cmd>"` to preserve internal quotes.

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

## recall — memory recall

```
icmg recall <query>
    [--limit N]            # default 10
    [--topic PREFIX]       # filter by topic prefix
    [--zone Z]             # restrict corpus to zone
    [--semantic]           # paraphrase-tolerant matching
    [--alpha N]            # blend 0..1 (default 0.5)
    [--pure]               # vector only
    [--all-projects]       # iterate registered projects
    [--fuzzy]              # fuzzy fallback
    [--at-commit SHA]      # filter to memories stored at a specific git commit (prefix ok)
    [--no-dedup]           # show nodes already returned this session (default: suppress)
    [--explain]            # show ranking detail
    [--history]            # show recent queries instead
    [--json]
```

`--semantic` requires the optional embedder to be configured. Falls back gracefully when not.
`--at-commit` matches memories whose `git_sha` starts with the given prefix (e.g. `abc1234`).
`--no-dedup` disables in-session deduplication — by default, nodes already surfaced in this session are suppressed to prevent identical results flooding multi-turn context.

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
icmg rule show <id>
icmg rule remove <id>
icmg rule enable  <id>
icmg rule disable <id>
```

### Trial / supersession lifecycle (v0.43.0)

When adding a stricter rule that replaces an older one, use `supersede` to run a trial period.
After N prompts without complaint the old rule is auto-deleted.

```
icmg rule supersede <new_id> <old_id> [--trial N]
    # new_id supersedes old_id; trial = N quiet prompts before auto-confirm (default 5)

icmg rule status
    # show all rules in trial with progress bar: [####......] 4/10 prompts

icmg rule revert <new_id>
    # cancel trial — delete new rule, restore old rule's priority

icmg rule trial-tick
    # increment trial counters; called automatically by UserPromptSubmit hook
```

**Flow example:**

```
icmg rule add --type enforcement --name "strict-no-read" --content '{"warn":0,"block":0}' --priority 100
# → id = 42  (stricter rule)
icmg rule supersede 42 37 --trial 10   # old rule was id 37
icmg rule status                        # watch progress
# after 10 prompts without user reverting → old rule 37 auto-deleted, rule 42 confirmed
```

---

## run — Tkil command filter

```
icmg run [options] <command...>

  --raw         # no filtering
  --json        # JSON output with metadata
  --dry-run     # show detected filter without running
  --stream      # real-time line-by-line output; filter summary appended at end
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
icmg sp link <file>                # Phase 21: scan file for EXEC <sp> → calls edges
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

## copy — Zero-output-token file copy (Phase 83)

Eliminates Claude output-token cost for file creation from existing sources.
Instead of Claude generating file content as output tokens, icmg copies
content directly — cutting output token spend by up to 97% per write.

```
icmg copy --from <src> [options]

  --from <file>        Source file (required)
  --lines A-B          Line range, 1-indexed inclusive (default: whole file)
  --to <file>          Destination (default: stdout)
  --append             Append to destination instead of overwrite
  --insert-at N        Insert before line N in destination
  --dry-run            Preview action without writing
```

**When to use:**
- New file that's largely based on an existing template or similar file
- Duplicate-and-modify pattern (copy scaffold, then apply edits)
- Appending boilerplate blocks from existing sources

**Token math:** A 150-line file write = ~1,500 output tokens. With `icmg copy` = ~15 tokens for the instruction. **97% output token reduction** per applicable write.

```bash
# Clone a template file
icmg copy --from src/template_cmd.cpp --to src/new_cmd.cpp

# Copy lines 1-40 (license + includes) into new test file
icmg copy --from tests/cli/test_store_cmd.cpp --lines 1-40 --to tests/cli/test_new.cpp

# Append boilerplate block
icmg copy --from src/base.cpp --lines 100-130 --to src/derived.cpp --append

# Insert scaffold before line 10
icmg copy --from template.cpp --lines 5-20 --to target.cpp --insert-at 10
```

---

## verify — Verification audit trail (Phase 22)

```
icmg verify --command "<cmd>" [--phase N]    # run + record
icmg verify show [--phase N]                 # list recorded
icmg verify gate [--phase N]                 # exit 0 if all pass
```

Records: exit code, output hash (fast hash), first 1KB of output, duration.

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

## Semantic Compression (Phase 39, v0.19+)

Reversible glossary-based prompt compression. Cuts dynamic-context tokens 30–60% on logs, diffs, and dumps. Lossless by default; round-trip exact.

### `icmg compress`

```bash
icmg compress < input.txt > compressed.txt
icmg compress --aggressive --kind .log mybuild.log
icmg compress --threshold 4000 --json
icmg compress --stats
```

| Flag | Effect |
|---|---|
| `--aggressive` | Add boilerplate filler-strip (lossy but readable) |
| `--threshold N` | Skip if est-tokens < N (default 8000) |
| `--kind <ext>` | Hint content kind (`.log`, `.md`, `.cs`, …); auto-skip source code |
| `--force` | Bypass threshold + kind gate |
| `--stats` | Print 30-day cumulative token totals + savings % |
| `--json` | Emit machine-readable summary instead of compressed text |
| `-o <file>` | Write to file instead of stdout |

Output format:
```
<icmg-glossary v=1 hash=abc123…>
@P1=src/middleware/authentication/jwt_validator.cpp
$I1=AuthenticationMiddleware
</icmg-glossary>
<icmg-body>
…body using @P1 and $I1 aliases…
</icmg-body>
```

### `icmg expand`

```bash
icmg expand < compressed.txt > original.txt
icmg expand --hash <H>      # load glossary from db by content hash
icmg expand --lenient       # leave unknown aliases as-is
```

### Pipe patterns (recommended)

```bash
git diff HEAD~5 | icmg compress
icmg pack "fix login bug" | icmg compress --aggressive
icmg diff-summary --ref HEAD~10 | icmg compress > /tmp/pr-context.txt
```

### Skip rules (auto-applied)

| Trigger | Behavior |
|---|---|
| Input < threshold tokens | Pass-through unchanged |
| Content kind in source-code ext (`.cs/.cpp/.ts/...`) | Pass-through (lossy edit risk) |
| Input contains `<<CACHED>>...<</CACHED>>` sentinel | Pass-through (preserve Anthropic prompt cache) |

**Caveat:** `icmg compress` is *semantic* token reduction, not byte compression. gzip would produce binary that Claude tokenizes as garbage. The glossary approach cuts actual token count while staying reversible.

---

## fetch — URL download with content-aware reduction (v0.27+)

```bash
icmg fetch https://api.example.com/v2/docs           # auto-detect kind
icmg fetch https://example.com --kind html
icmg fetch https://api.github.com/repos/X/Y --kind json
icmg fetch https://example.com --refresh             # bypass cache
icmg fetch https://example.com --raw                 # skip reduction
icmg fetch <url> -o /tmp/page.txt
```

| Flag | Effect |
|---|---|
| `--kind <K>` | Force kind: `html`, `json`, `pdf`, `text`, `binary`, `auto` |
| `--refresh` | Bypass cache, re-download |
| `--raw` | Skip reduction, return raw body |
| `--max-bytes N` | Cap output (default 8192) |
| `--ttl N` | Cache TTL seconds (default 3600) |
| `--json` | Machine output with metadata |
| `-o <file>` | Write to file |

Reduction strategies:
- **HTML:** strip script/style/nav/aside/footer; extract `<main>` or `<article>`; collapse whitespace; entity decode
- **JSON:** pretty if <5KB, schema-mode (keys + types + sample values, depth 3) otherwise
- **PDF:** shellout to `multimodal/icmg_ingest.py` sidecar
- **Binary:** metadata only (size, FNV1a-64 hash)

Cache: `fetch_cache` table per URL+ETag, default 1h TTL. Repeat fetches hit cache instantly (100% saving).

Real-world savings: 87-91% on JSON / HTML in typical research workflow.

---

## update — Self-upgrade (v0.27+ pending-restart pattern)

```bash
icmg update --check       # compare current to latest release
icmg update --apply       # download + atomic swap
icmg update --rollback    # restore .bak (if present)
```

If the running binary is locked when `--apply` swaps:
- Writes `.pending-restart` flag + leaves `.new` file in place
- Prints recovery message
- Next `icmg <cmd>` invocation in any new terminal applies the swap silently

This eliminates the "file in use" hard failure that previously required manual `taskkill`.

Post-install also fetches release notes via GitHub API and prints a "WHAT'S NEW" + "AGENT NOTE" block so AI agents auto-discover new features.

---

## ingest — OCR images locally (v0.28+)

```bash
icmg ingest screenshot.png             # OCR text + 7d hash cache
icmg ingest --raw shot.png             # metadata only, no OCR
icmg ingest --refresh shot.png         # bypass cache
icmg ingest --json shot.png            # machine output
icmg ingest --min-chars 30 shot.png    # tune confidence threshold
```

| Flag | Effect |
|---|---|
| `--raw` | Skip OCR; print hash + size only |
| `--refresh` | Bypass cache, re-OCR |
| `--min-chars N` | Below → emit "vision-recommended" note (default 30) |
| `--ttl N` | Cache TTL seconds (default 604800 = 7d) |
| `--json` | Machine output |
| `-o <file>` | Write to file |

| Image type | Saving | Speed |
|---|---|---|
| Code / terminal screenshot | 90-95% | 5x faster than vision call |
| API doc screenshot | 75% | 3x faster |
| UI mockup | OCR limited; vision-recommended | — |

Cache: `image_cache` table per FNV1a image hash, 7d TTL. Repeat ingest = instant.
Requires Python `pytesseract` + `Pillow` + tesseract binary on PATH. Without sidecar → falls back to metadata mode.

---

## sync — Team memory + graph sharing via git-tracked JSONL (v0.29+)

Real team sharing of memory + graph. JSONL snapshots tracked in git, conflict-resolved via row_version optimistic locking.

```bash
# First-time setup per teammate
icmg sync init           # creates .icmg/sync/ + gitignore wiring

# Daily workflow
icmg store --topic decisions-x "..."
icmg sync push           # DB -> .icmg/sync/<table>.jsonl (sorted by content_hash)
# git add .icmg/sync && git commit && git remote-update

# Teammate after fetching
icmg sync pull           # .icmg/sync/ -> his data.db
# [icmg sync pull] +1 inserted, ~0 updated, 0 conflicts

icmg sync status         # snapshot row counts + last push metadata
icmg sync merge /path/old.db --yes      # adoption case: fold solo DB into team
```

| Subcommand | Purpose |
|---|---|
| `init` | Create `.icmg/sync/`, update `.gitignore` to track shared dir |
| `push` | DB → deterministic JSONL snapshot |
| `pull` | JSONL → DB merge with conflict resolution |
| `merge <db>` | Fold another local data.db (solo→team adoption) |
| `status` | Snapshot row counts + last push metadata |

Conflict resolution table:

| Local vs Remote | Action |
|---|---|
| local missing | INSERT |
| local v < remote v | UPDATE (remote wins) |
| local v == remote v | no-op |
| local v > remote v | keep local, log conflict to stderr |
| `--force-remote` | overwrite local on any version diff |

Tables synced: `memory_nodes`, `graph_nodes`. NOT synced: per-user state (recall freq, last_used), embeddings, caches. Embeddings regenerate locally via `icmg embed memory --backfill` after pull.

---

## batch — Anthropic Batch API spec emitter (v0.26+)

```bash
icmg batch --task "summarize A" --task "summarize B" --no-think -o batch.json
icmg batch --file tasks.txt --concise --max-tokens 1500
```

| Flag | Effect |
|---|---|
| `--task "<text>"` | Add one task (repeat) |
| `--file <path>` | Read tasks from file (one per line) |
| `--model <name>` | Default `claude-sonnet-4-5` |
| `--max-tokens N` | Default 2000 |
| `--no-think` / `--concise` / `--caveman` | Inject directive into each request |
| `--custom-id-prefix P` | Default `task` → `task-1`, `task-2`, … |
| `-o <file>` | Write JSON to file |
| `--emit-json` | Default behavior; output Batch API JSON |

Pipe pattern:
```bash
icmg batch --file tasks.txt --no-think | curl -X POST \
  https://api.anthropic.com/v1/messages/batches \
  -H 'x-api-key: $ANTHROPIC_API_KEY' \
  -H 'anthropic-version: 2023-06-01' \
  -H 'Content-Type: application/json' --data @-
```

**Discount:** ~50% on bulk via Anthropic Batch API + 24h SLA.

---

## Tool-call cache (v0.26+)

`pack` results are auto-cached locally for 5 min keyed by content hash. Repeat queries skip recompute entirely.

```bash
icmg pack "fix login bug"            # first call: compute + store
icmg pack "fix login bug"            # second call within 5min:
[icmg pack] cache HIT (skip recompute)

icmg pack "fix login bug" --no-cache # bypass cache
ICMG_NO_CACHE=1 icmg pack "..."      # bypass globally
```

Cache table: `tool_call_cache` in project DB. TTL fixed at 300s (matches Anthropic prompt-cache window). Pruned automatically on next call.

---

## savings — Unified telemetry dashboard (v0.23+)

```bash
icmg savings                         # 30-day console rollup
icmg savings --window 7d
icmg savings --html -o /tmp/dash.html
icmg savings --json
icmg savings --rate-input 0.80 --rate-output 4.00   # Haiku rates
```

Aggregates per-layer telemetry (filter, compression, thinking-budget) → with-vs-without comparison + estimated $ saved.

---

## shrink — Auto-detect content + intelligent reduction (v0.24+)

```bash
huge_grep_command | icmg shrink
icmg shrink --kind compress < log.txt   # force semantic compression
icmg shrink --threshold 0 < input.txt   # always shrink
```

Detects content type (grep, build log, SQL, JSON, generic) and applies the best strategy. Falls back to head+tail+byte-count.

Auto-installed as PostToolUse:Bash hook by `icmg init` so Claude Code's huge command outputs (>50KB) shrink automatically.

---

## whats-new — Release notes (v0.25+)

```bash
icmg whats-new                       # current version
icmg whats-new --tag v0.24.0         # specific version
icmg whats-new --since v0.20         # range, oldest-first
icmg whats-new --json
```

Called automatically by `icmg update --apply` post-install. AI agents reading the upgrade output auto-discover new features.

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

## claudemd — CLAUDE.md context graph integration (v0.42.0)

Import CLAUDE.md sections into the context-node graph for BM25-powered on-demand injection.

```
icmg claudemd import [--file PATH] [--dry-run]
icmg claudemd diff  [--file PATH]
icmg claudemd slim  [--file PATH]
```

| Subcommand | Description |
|---|---|
| `import` | Parse sections, upsert into context_nodes; auto-detects global `~/.claude/CLAUDE.md` + project `./CLAUDE.md` |
| `diff` | Show sections where stored content differs from live file |
| `slim` | Print a pointer-only stub that replaces the full CLAUDE.md in session start |

Auto-import runs during `icmg init` — no manual step needed on first setup.

**Tier auto-detection:** sections containing "rule", "always", "never", "critical", "enforce" → `hot`; others → `cold`.

---

## context-node — BM25 context matching (v0.42.0)

Search context_nodes by query; used by the UserPromptSubmit hook for automatic context injection.

```
icmg context-node match <query> [--tier hot|cold|skill|all] [--top N] [--min-score F] [--fmt text|json|additionalContext]
icmg context-node get   <node_key>
```

`--fmt additionalContext` emits Claude Code hook JSON (`hookSpecificOutput.additionalContext`) — used by the session hook directly.

---

## rule-eval — PreToolUse enforcement evaluation (v0.42.0)

Evaluate a single tool call against size rules. Used by the `icmg-rule-enforce.sh` PreToolUse hook.

```
icmg rule-eval <tool> <file> [--lines N]
icmg rule-eval ping
```

Exit codes: `0` = ALLOW/WARN (additionalContext JSON emitted on warn), `2` = BLOCK (permissionDecision deny).

---

## rule-daemon — Rule enforcement daemon lifecycle (v0.42.0)

Persistent daemon process that evaluates PreToolUse events at ~2–5 ms IPC vs 50 ms subprocess per call.

```
icmg rule-daemon start   [--db PATH]
icmg rule-daemon stop
icmg rule-daemon status
icmg rule-daemon reload
icmg rule-daemon strict [on|off|status]   # v0.43.0: toggle strict mode
icmg rule-daemon run     # foreground (used internally by start)
```

Default thresholds: warn ≥ 200 lines, block ≥ 500 lines. Override via `rules` table (`rule_type='enforcement'`).
Fails open — if daemon is unreachable, every tool call is ALLOW.

### Strict mode (v0.43.0)

```
icmg rule-daemon strict on      # block ALL Read/Glob/Grep regardless of size
icmg rule-daemon strict off     # back to threshold mode (warn≥200, block≥500)
icmg rule-daemon strict status  # show current mode
```

Strict mode forces `permissionDecision: deny` for every file read. Bypass per-call with env `ICMG_STRICT_BYPASS=1`.

---

## skill — Skill index management (v0.42.0)

Index skill `.md` files from `~/.claude/` and `.claude/` into context_nodes for BM25 suggestion.

```
icmg skill index  [--dir PATH] [--force]
icmg skill list   [--json]
icmg skill search <query>
```

The UserPromptSubmit hook calls `icmg skill search` and injects top matches as `additionalContext`, surfacing relevant skills automatically.

---

## knowledge — Knowledge browser + CRUD (v0.42.0)

Browse and manage context_nodes (rules, context sections, skills) from the CLI or HTML dashboard.

```
icmg knowledge list   [--type context|skill|hot|cold|all] [--inactive] [--json]
icmg knowledge get    <node_key>
icmg knowledge add    --title TITLE --content TEXT [--tier hot|cold|skill] [--tags JSON] [--source FILE]
icmg knowledge edit   <node_key> [--title T] [--content C] [--tier T] [--tags J] [--active yes|no]
icmg knowledge delete <node_key> [--confirm]
icmg knowledge --html
```

`--html` opens the unified dashboard at `http://127.0.0.1:8080/knowledge` (requires `icmg serve`).
Dashboard shows 3 tabs — **Knowledge** (hot/cold nodes), **Skills** (indexed skills), **Rules** (stored rules) — with per-tab CRUD and KPI strip.
REST API: `GET/POST /api/knowledge`, `GET/PUT/DELETE /api/knowledge/<key>`, `GET /api/rules`, `POST /api/rules/toggle`.

---

## serve — Embedded HTTP dashboard (v0.38.0 / unified v0.42.1)

Launch a local read-only dashboard served from the project DB.

```
icmg serve [--port N] [--host BIND] [--no-open]
```

| Option | Default | Description |
|---|---|---|
| `--port N` | 8080 | TCP port |
| `--host BIND` | 127.0.0.1 | Bind address |
| `--no-open` | off | Skip auto-open browser |

**Endpoints:**

| Path | Description |
|---|---|
| `/` | Memory + graph + audit dashboard |
| `/knowledge` | Unified dark-theme dashboard (3 tabs: Knowledge \| Skills \| Rules) |
| `/api/audit` | Audit metrics JSON |
| `/api/memory?n=N` | Last N memory nodes |
| `/api/graph?n=N` | Top N graph nodes |
| `/api/recall?q=X` | BM25 recall |
| `/api/knowledge/list?tier=` | List context nodes (filter by tier) |
| `/api/knowledge/add\|update\|delete\|toggle` | CRUD for context nodes |
| `/api/rules` | List all rules |
| `/api/rules/toggle?id=N&active=0\|1` | Enable/disable a rule |

---

## See Also

- `README.md` — overview + token-efficiency rationale
- `AGENTS.md` — usage guide for AI coding agents
- `CLAUDE.md` — Claude Code-specific project context
- `PROGRESS.md` — phase tracker
- `docs/plans/*.md` — per-phase implementation plans
