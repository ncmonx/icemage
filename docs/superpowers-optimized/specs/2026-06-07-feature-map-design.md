# Feature-Map (command "you-are-here" maps) — Design

**Date:** 2026-06-07
**Status:** Design (brainstorm) — pending user review → writing-plans.
**Origin:** kak Cahyo hotel metaphor — "every hallway/room has a small local map." icmg grows → capabilities forgotten/duplicated (live proof: Claudy rebuilt `context-budget` unaware it existed). Anchors #32006 (surfacing gap), #32007 (feature-map vision).

---

## 1. Problem

Relatedness today (`core::rankCommands`) maps **prompt-intent → command** (Jaccard + nameRecall over name+desc), surfaced per-prompt (`icmg suggest`, relevant-command hook) + the CLAUDE.md decision-tree (a global index to memorize). Two gaps:
- **No command → neighbor map** ("you are at X; nearby: Y, Z"). You must already know X exists.
- **No surfacing at the model's BUILD/action decision-time** — the dup happened while building, not on a user prompt.

## 2. Scope & decisions (user-approved)

- **Relations = DERIVED/auto** (zero new per-command data): a command's neighbors = `rankCommands(name + " " + desc, allCommandDocs)` minus self. Auto-maintains, **no rot**, reuses the existing engine. (Curated overrides = a later, optional layer — not now.)
- **Iteration 1 (this spec, ramping):**
  1. `icmg map <cmd>` — "you-are-here" + top-N derived neighbors.
  2. **Pre-build reflex rule** (CLAUDE.md baku): before creating a new command, run `icmg suggest "<purpose>"` / `icmg map <near>`; if a close match exists, EXTEND, don't duplicate.
- **Later phases (designed, not built now):**
  3. `--help` footer: each command's help ends with `↪ related: X, Y, Z` (derived).
  4. Output footer after a run — **opt-in/gated** (env or small-output only) to avoid per-run noise.

### Non-goals
- ✗ Curated/declared relations (rot risk; derived first).
- ✗ Always-on output footer (noise) — opt-in only when built.
- ✗ A hook that auto-detects "about to create a command" (no such hook point; reflex rule covers it for now — see failure mode 2).

## 3. Architecture & data flow

```
icmg map <cmd>
  └─ registryDocs() (name+desc of all commands; already used by suggest)
       ├─ found cmd  -> neighbors = rankCommands(cmd.name+" "+cmd.desc, docs, N+1) \ {cmd}
       │                print "you-are-here: <cmd> — <desc>" + neighbors
       └─ not found  -> treat <cmd> as intent -> rankCommands(intent, docs, N) (fallback = suggest)
```

Reuses `core::rankCommands` + `registryDocs()` (suggest_cmd.cpp). New code is thin.

## 4. Interfaces

```cpp
// Pure (testable): neighbors of a command = top-N similar docs excluding self.
// docs = all command {name,desc}; returns names (or CmdHit) ranked, self removed.
std::vector<core::CmdHit> neighborsOf(const std::string& cmdName,
                                      const std::vector<core::CmdDoc>& docs, int n);
```

- `icmg map <cmd> [--top N]` (default N=6): the command wraps `neighborsOf` (found) or `rankCommands` (fallback) + prints.
- Pre-build reflex = documentation (CLAUDE.md "## Before adding a command" rule), not code.

## 5. Error handling
- Unknown `<cmd>` → fallback to intent-ranking (still useful), note "(no exact command; nearest by intent)".
- Empty registry / rank error → "no neighbors" message, exit 0 (advisory tool, never hard-fail).

## 6. Testing (TDD)
- `neighborsOf`: returns N, excludes self, ranks by similarity (fixture docs: context-budget near savings/govern; unrelated far). Empty docs → empty.
- map command: found path prints you-are-here + neighbors; unknown path falls back.

## 7. Failure-mode check (adversarial)

| # | Failure | Severity | Disposition |
|---|---|---|---|
| 1 | Derived neighbors noisy (weak text overlap for some cmds) | Minor | Advisory only; better than nothing; curated overrides = later layer. |
| 2 | **Pre-build reflex relies on the model REMEMBERING to run it** — same memory-dependence the project fights | **Minor (accepted, iteration 1)** | Durable fix = a hook nudge at command-creation, but no such hook point exists cleanly. For now: anchor as baku rule + `icmg map`/`suggest` exist. Documented limitation; a creation-time hook is a future phase. |
| 3 | `icmg map` itself becomes an unused feature (irony) | Minor | Also reached via the reflex rule + later --help footer; surfaced in `icmg --help`. |

No Critical failures. #2 is the real weak point but accepted for iteration 1 (durable hook = later); the tool + rule are a strict improvement over today.

## 8. Phasing
- **M1 (this iteration):** `neighborsOf` + `icmg map <cmd>` + tests + CLAUDE.md pre-build reflex rule.
- **M2 (later):** `--help` footer (derived `↪ related`).
- **M3 (later):** opt-in output footer (gated).
- **M4 (later, durable):** creation-time hook nudge (research a hook point).
