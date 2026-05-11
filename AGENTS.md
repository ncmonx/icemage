# AGENTS.md — using icmg from inside an AI coding agent

For Claude Code, Cursor, Copilot, Aider, Continue, Codespaces, anything that drives a model. This file tells the agent the right reflexes.

> **The rule of thumb:** before reading a big file or running a noisy command, ask `icmg` first.

---

## Post-upgrade reflex (auto)

When user runs `icmg update --apply`, the binary now prints a "WHAT'S NEW" block with the new release's full notes. **You MUST scan that block** for new commands or flags, then:

1. Run `icmg --help` to refresh your view of the command list
2. Run `icmg <new-cmd> --help` for any unfamiliar names
3. Adopt new patterns where they apply to the current task
4. If user invokes upgrade without specific instructions ("upgrade", "update icmg"), you are still expected to read the WHAT'S NEW block and surface a one-line summary plus the most relevant new feature for the user's project

If the upgrade output is missing the block (e.g. network failure), run:
```bash
icmg whats-new                # current version notes
icmg whats-new --since v0.20  # all releases since v0.20
```

## The parallel-first rule

If you have 2+ independent steps, run them concurrently. Sequential is a regression.

```bash
# Wrong
icmg run ctest
icmg run npm test

# Right
icmg parallel \
    --task "icmg run ctest" \
    --task "icmg run npm test"
```

If two steps don't share a file write and don't feed each other's output, parallel.

---

## Decision tree

| Situation | Reach for |
|---|---|
| Starting any task | `icmg pack "<task>"` |
| Need a single file's context | `icmg context <file>` |
| Need a specific symbol | `icmg graph symbol <name>` |
| Who depends on this? | `icmg graph reverse-impact <name>` |
| Reviewing a big diff | `icmg diff-summary` |
| Running anything noisy (build, test, query, log tail) | `icmg run <cmd>` |
| Hit an error you've seen before | `icmg explain "<error>"` |
| Need recall over project memory | `icmg recall "<query>"` |
| Want to delegate a sub-task | `icmg agent "<task>"` |
| Sending a 30K+ token blob to the model | pipe through `icmg compress` |
| Big stable preamble repeated each turn | `icmg pack ... --cache-prefix` |
| Routine task that doesn't need analysis | `icmg pack ... --auto-think` |
| Saving state before `/clear` or `/compact` | `icmg session save <name>` |

That covers ~95% of the day. Run `icmg --help` for the rest.

---

## Token-saving substitutions (your default reflexes)

When you'd normally do this... do this instead:

| Default reflex | Better via icmg |
|---|---|
| `Read` a large file | `icmg context <file>` |
| `Grep` a symbol across the repo | `icmg graph symbol <name>` |
| `git diff` (verbose) | `icmg diff-summary` |
| Manual import exploration | `icmg context <file>` (already includes them) |
| Search internal docs / notes | `icmg recall "<query>"` |
| Run `npm test` then page through 1000-line output | `icmg run npm test` |
| Grep history for similar bugs | `icmg explain "<error>"` |
| Send a big log/diff to the model raw | pipe through `icmg compress` |

---

## Compression rules

`icmg compress` is for **dynamic context** — logs, diffs, dumps, history. Never for source files about to be edited.

- Auto-skips small inputs and source-code files
- Pass-through preserves prompt-cache regions (don't worry about it)
- Lossless by default; `--aggressive` is opt-in

When in doubt, pipe and check the savings number it prints.

---

## Thinking-budget rules

Models think before they answer. That thinking is billed. Most of it is wasted on routine work.

- Routine task (rename, format, list, lookup) → `pack ... --auto-think` will turn off the analysis pass
- Hard problem (debug, refactor, design) → leave thinking on
- Reply needs to be short → `pack ... --concise`
- Reply needs to be ultra-short (caveman) → `pack ... --caveman`

You can also force it with `--no-think`. Same flags work on `icmg agent`.

---

## External downloads (auto-route)

Don't use WebFetch / curl directly when icmg has the URL covered:

| Default reflex | Use instead | Saving |
|---|---|---|
| WebFetch \<docs URL\> | `icmg fetch <url>` | 70-90% on HTML pages |
| curl \<api endpoint\> | `icmg fetch <url> --kind json` | 80% on big JSON |
| Read PDF from URL | `icmg fetch <url> --kind pdf` | 85% via text extract |
| Re-fetch same URL twice | (already cached) | 100% second hit |

Fetch is content-aware (strips HTML chrome, JSON-schema-summarizes >5KB payloads, hashes binaries). Cache TTL 1h per URL+ETag. Bypass with `--refresh`.

---

## Cache + batch (saves recompute and bulk cost)

- **Repeat queries in same session:** `icmg pack` auto-caches results for 5 min. Identical query → instant skip.
  - Disable per-call: `--no-cache` flag
  - Disable globally: `ICMG_NO_CACHE=1` env
- **Bulk operations (≥3 similar tasks):** use `icmg batch --task ... --task ...` to emit Anthropic Batch API spec → 50% discount.
  - Pipe `icmg batch ... --emit-json` straight into `curl /v1/messages/batches`.

---

## Image inputs (auto-route via icmg ingest)

When user pastes / uploads a screenshot, prefer local OCR before invoking model vision:

| Default reflex | Use instead |
|---|---|
| Send screenshot to model vision | `icmg ingest <file>` first; pass extracted text |
| Re-process same screenshot in session | (already cached for 7d per image hash) |

Only fall back to vision when:
- `icmg ingest` reports low confidence (`< 50%` or `< 30 chars` of alnum)
- Image is a UI mockup / chart / diagram (visual structure, not text)

`icmg ingest` requires Python `pytesseract` + `Pillow` + tesseract binary. Without sidecar → emit raw image as fallback.

---

## Team workflow (icmg sync)

Memory + graph shareable across teammates via git-tracked JSONL snapshots:

| When | Reflex |
|---|---|
| First time on this project | `icmg sync init` once |
| After making decision worth sharing | `icmg sync push` then `git add .icmg/sync && git commit` |
| After fetching teammate's changes | `icmg sync pull` |
| Joining team with prior solo work | `icmg sync merge /path/old.db --yes` |

What syncs: `memory_nodes`, `graph_nodes`. NOT synced: per-user state (recall freq, last_used), embeddings, caches. After pull, run `icmg embed memory --backfill` to regenerate embeddings locally.

Conflicts resolved by row_version optimistic locking (auto-bumped on writes). Newer wins; equal versions = no-op; local-newer kept with warning. `--force-remote` overrides.

---

## Memory conventions

Use these prefixes when storing memory so future sessions can find things:

- `decisions-<topic>` — architectural choices, rejected approaches
- `errors-resolved-<area>` — fix you don't want to redo
- `prefs-<scope>` — user preferences, conventions
- `memoir:<title>` — long-form essays, post-mortems

Importance levels: low / med / high / critical. High and above survive eviction.

---

## Anti-patterns

Don't:
- Read a file you could `context` or `graph symbol`
- Run a noisy command without `icmg run`
- Send a 30K+ blob to the model without piping through `compress`
- Re-explain project context every turn — pack it once with `--cache-prefix`
- Pay for thinking on a one-line rename

Do:
- Pack once, drill in twice
- Trust the filter (it knows the noise patterns)
- Cache the stable, compress the dynamic
- Save snapshots before risky operations

---

## Local-first guarantees

- Per-project SQLite — never leaves your machine
- No cloud sync, no telemetry phoned home
- Apache-2.0 — fork it, audit it, ship it

<!-- icmg:start -->
## icmg routing (auto-inserted by `icmg init`)

This project uses **icmg** for token-efficient code navigation.

### ABSOLUTE RULE — icmg FIRST, ALWAYS

**Before any native tool call (Read / Bash / Grep / Glob / WebFetch), STOP and check the decision tree below.** If an `icmg` command serves the same need, you MUST use icmg. No exceptions, no "small file" excuses, no "just this once."

Order of resolution for every action:

1. **Is there an icmg command for this?** → run it
2. **No icmg command?** → run native tool
3. **icmg command failed?** → diagnose with `icmg doctor` first; only fall back to native when icmg explicitly errors

This is enforced at hook level (strict mode auto-on). Native calls that have an icmg equivalent are blocked with a redirect message. Do not waste tokens trying native first — the hook will block, you'll re-issue via icmg, you've burned a turn.

**Common slip-ups that cost tokens:**
- Reading a big file with native Read instead of `icmg context <file>` → 80%+ saved
- `grep -r` instead of `icmg run grep ...` → unfiltered noise
- WebFetch instead of `icmg fetch <url>` → no cache, no reduce
- `cat large.log` instead of `icmg compress < large.log` → no glossary
- Running 3 reads sequentially instead of `icmg parallel` → 3-6× wall-clock loss

### CRITICAL: parallel-first rule

**If you have 2+ independent tasks (independent files, independent checks, independent recalls), ALWAYS run them via `icmg parallel`.** Do NOT run sequentially. This is non-negotiable — sequential runs waste wall-clock and miss the I/O parallelism win (3-6× speedup on typical paths).

```bash
# Wrong: sequential — waits each
icmg verify --command "ctest"
icmg verify --command "cmake --build build"
icmg run npm test

# Right: parallel — all run concurrently
icmg parallel \
    --task "icmg verify --command 'ctest'" \
    --task "icmg verify --command 'cmake --build build'" \
    --task "icmg run npm test"
```

Heuristic: if your next 2+ steps don't share a file write or depend on each other's output, use `icmg parallel`.

### Decision tree

| Want to | Use |
|---|---|
| **Run 2+ independent steps** | `icmg parallel --task "..." --task "..."` (default — see rule above) |
| Read a large file | `icmg context <file>` (graph + symbols + memory) |
| Find a function | `icmg graph symbol <Name>` (30 lines, not 800) |
| Trace impact | `icmg graph reverse-impact <Name> --depth 5` |
| Search code | `icmg run grep ...` (auto-filtered) |
| Recall past decision | `icmg recall "<query>"` |
| Paraphrase recall | `icmg recall "<query>" --semantic` |
| Recall at specific commit | `icmg recall "<query>" --at-commit <sha>` |
| Start new task | `icmg pack "<task>"` (4KB context bundle) |
| Delegate to LLM | `icmg agent "<task>"` (pack→prompt→user-CLI) |
| Run noisy command | `icmg run <cmd>` (Tkil filter — 60-90% smaller) |
| Big git diff | `icmg diff-summary --ref HEAD~5` |
| Errored before? | `icmg explain "<error>"` |
| List directory | `icmg ls [path]` |
| Clone existing menu | `icmg parity <ref> <new>` (catch missed handlers) |
| Generate scaffold | `icmg template extract <ref> --save-as X` then `icmg template apply X --to <new>` |

**Auto-rewrite hook installed.** Raw `grep`, `node`, `cargo build`, `pytest`, etc. auto-redirect through `icmg run`. Bypass with `RAW=1 <cmd>`.

### Persist learnings (always)
- Fixed a bug? `icmg known-issue add "<pattern>" --fix "<resolution>"`
- Made a decision? `icmg store --topic decisions-<feature> "<rationale>"`
- Long-form rationale (post-mortem, ADR)? `icmg memoir add --title T --content-file F`

Full reference: run `icmg --help` or see https://github.com/ncmonx/icm-graph
<!-- icmg:end -->
