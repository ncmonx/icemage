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

You can also force it with `--no-think`.

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
