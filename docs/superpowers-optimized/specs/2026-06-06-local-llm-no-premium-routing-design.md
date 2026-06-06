# Local-LLM → "No-Premium Backup Brain" — Design

> Date: 2026-06-06
> Status: APPROVED (kak Cahyo, 2026-06-06)
> Author: Claudy (brain-side)
> Origin: `icmg llm` / llama backend telemetry = 0 calls (dead). Repurpose, not cut.
> Decision fork resolved: **B (repurpose)** over A (cut) / C (leave).

## Problem

The local LLM (`icmg llm`, llama.cpp backend, `ICMG_USE_LLAMA=ON`) is fully built and
wired into 4 callers (`ask_cmd`, `chat_cmd`, `bundle_cmd` route gating, `atom_llm.hpp`),
yet telemetry reports **0 calls, 0 tok/s, warm-pool cold**. Root cause: icmg is driven
*through* Claude Code. Claude is present on every interactive turn and is strictly better
than a local dolphin-8b, so nothing ever needs the local model. The feature only earns its
keep where **no premium LLM is present**: cron, daemon, headless agent, offline.

This feature was built earlier (v1.31.0) at the user's request, before the assistant had a
persona. The intent here is to give that prior work a real home — not waste it.

## Goal

Local LLM activates **only when no premium LLM (Claude) is available in this execution**,
or when the user explicitly asks for local. Claude always wins when present. All existing
safety gating is preserved.

## Key reframe (vs. initial brief)

The routing signal is **`premium_available`**, NOT raw `headless`. "Headless" is merely the
common case of premium-absent. Using `premium_available` closes the hole where
`agent --exec` runs headless *but* a Claude API key is configured — there, premium wins.

Local fires iff: **`(!premium_available) OR explicit_local`** (subject to all existing
hard-rules / heuristics / cooldowns).

## Scope

- New `core/exec_context.hpp` — run-mode + premium-availability signal.
- Extend `smart_router` `CallContext` with `premium_available` + `explicit_local`.
- One new gate rule in `routeFor`.
- Wire 4 no-premium paths to set the signal.
- TDD throughout.

## Non-goals

- `LLM_CLOUD` offload (reserved for future).
- Force-local in interactive sessions for cost savings — `ask --backend=local` already
  covers manual opt-in; we chose Claude-always-wins.
- Env-sniffing Claude presence (CLAUDECODE etc.) — explicit signal only; sniffing is brittle.
- Persisting `session-disable` across cron runs (documented minor limitation; defer).

## Architecture & Data Flow

### 1. `core/exec_context.hpp` (new)

```cpp
namespace icmg::core {
enum class RunMode { INTERACTIVE, HEADLESS };

// Lazy: reads env ICMG_RUN_MODE ("headless"/"interactive") on first call.
// Programmatic override via setRunMode() (in-process entrypoints).
RunMode currentRunMode();
void    setRunMode(RunMode);

// premium_available defaults TRUE (safe = assume Claude present, local OFF).
// Entrypoints that KNOW premium is absent flip it false explicitly.
bool premiumAvailable();
void setPremiumAvailable(bool);
}
```

- **Default = premium ADA / local OFF.** A no-premium path must *explicitly* declare itself.
  Consequence: if a cron job forgets to set it, local stays dormant (no regression, no
  runaway local inference). Safe-by-default.
- Env `ICMG_RUN_MODE=headless` is the cross-process carrier (cron/daemon spawn child icmg
  with this env). `setPremiumAvailable(false)` is the in-process carrier.

### 2. `smart_router` `CallContext` extension

```cpp
struct CallContext {
    // ... existing fields ...
    bool premium_available = true;   // Claude/premium present in this execution
    bool explicit_local    = false;  // user explicitly chose local (ask --backend=local, chat)
};
```

### 3. `routeFor` new gate

Inserted **after** existing hard-rules (hot→regex, cache, build_has_llama, user_disabled,
session-disable, cooldowns), **before** the small-input/RAM heuristics:

```cpp
// Premium present and not an explicit local request → reserve local for no-premium.
if (ctx.premium_available && !ctx.explicit_local)
    return { Route::REGEX, "premium (Claude) present — local reserved for no-premium/explicit" };
```

All other layers unchanged. Net rule: **local fires iff `(!premium_available || explicit_local)`**
and everything else (hot, cache, build, opt-out, cooldown, small-input, RAM) still gates.

### 4. Wiring the 4 paths

| Path | Sets | Local fires when |
| --- | --- | --- |
| cron atomize memory | `ICMG_RUN_MODE=headless` on spawned child → `premium_available=false` | every run |
| cron summarize / digest | same | every run |
| `agent` native-local backend | `premium_available = (claude CLI/api_key runnable)` ; `--local` → `explicit_local=true` | premium absent OR `--local` |
| PreCompact COLD summarize | `premium_available = !isHeadless()` | only in daemon/headless compact (rare in interactive Claude) |

`ask --backend=local` and `chat` set `explicit_local = true` → continue to work in
interactive sessions (not blocked by the new gate).

### 4b. `icmg agent` native-local backend (the headline integration)

Today `icmg agent` is a **proxy**: pack → prompt → spawn external CLI (`claude --print`).
With no Claude CLI present it fails outright. New behavior:

```
icmg agent "task":
  1. pack → prompt                               (unchanged)
  2. premium_available = agent.command runnable AND api_key present
  3. routeFor({ kind="agent", tier=WARM, premium_available, explicit_local=--local, ... })
  4. Route::LLM_LOCAL → WarmPool::infer() in-process (NO external proc)   [NEW]
     else            → external CLI                                       (unchanged)
  5. store decision                              (unchanged)
```

This gives the local model its first real consumer: autonomous sub-tasks that run with
zero external dependency (cron / daemon / offline can delegate to `icmg agent`).

**Decision — advisory-only (locked):**
- Native-local backend produces **text output only** (advice / summary / answer).
- **`--exec` (edit/write/bash) stays external-CLI-only.** `local-route + --exec` is
  **refused** with a clear message: a weak 8b model must never auto-edit files. `--exec`
  requires a premium agentic CLI.

**Context overflow — truncate + warn (locked):**
- Local model window is small (~8k for dolphin-8b). If the assembled prompt exceeds the
  model window, **truncate to fit + log** `"local context truncated to NNN tokens"`.
  No crash, honest about the loss.

## Interfaces / Contracts

- `core/exec_context.hpp`: `currentRunMode()`, `setRunMode()`, `premiumAvailable()`,
  `setPremiumAvailable()`. Lazy env read, idempotent, thread-safe-enough (single-writer at
  startup; readers after).
- `CallContext`: 2 new bool fields, both defaulting to the safe/back-compat value
  (`premium_available=true`, `explicit_local=false`) → existing callers compile unchanged
  and keep prior behavior (which was: 0 local calls interactive).
- `routeFor`: same signature; one added branch. Pure, sub-ms preserved.

## Error Handling

- Local inference failure on a no-premium path → existing fallback to REGEX (caller already
  handles `Route::REGEX`). Cron job logs, does not crash.
- cold-load fail ×2 → existing `session-disable`. In cron each run is a fresh process, so
  the disable is per-run (re-attempts next night). Accepted minor limitation.
- `ICMG_RUN_MODE` malformed/absent → treated as INTERACTIVE / premium-present (safe default).

## Testing Strategy (TDD — failing test first)

- `tests/core/test_exec_context.cpp`:
  - default `currentRunMode()==INTERACTIVE`, `premiumAvailable()==true`.
  - `ICMG_RUN_MODE=headless` → `currentRunMode()==HEADLESS`.
  - `setPremiumAvailable(false)` reflected by `premiumAvailable()`.
  - malformed env → INTERACTIVE.
- `tests/llm/test_smart_router.cpp` (extend):
  - premium_available=true, explicit_local=false → REGEX (new rule).
  - premium_available=false → LLM_LOCAL (warm, loaded, ok RAM).
  - premium_available=true, explicit_local=true → LLM_LOCAL.
  - tier=HOT, premium_available=false → REGEX (hot still wins over new rule).
  - existing cases (cache, build_has_llama=false, user_disabled, small-input, RAM) still pass.
- Wiring tests:
  - cron-spawn context → child reports `premiumAvailable()==false`.
  - `agent` with API key present → premium_available stays true → external CLI; no API/`--local` → LLM_LOCAL native.
- Agent native-local tests:
  - no-premium + `agent "task"` → routes LLM_LOCAL, in-process infer, no external spawn.
  - `agent --local --exec` → **refused** with clear message (advisory-only rule).
  - prompt > model window → truncated + warn logged, no crash.

## Rollout / Migration

- Purely additive. No DB migration. No CMakeLists edit (GLOB picks new TUs).
- CI-lint `tools/lint_no_llm_in_hot.sh` unchanged — `exec_context.hpp` is not a router include
  in hot-path TUs; gate rule lives in `smart_router.cpp` only.
- Back-compat: defaults preserve current behavior (local was never used interactively anyway).

## Failure-Mode Check (adversarial)

1. **Cron child doesn't inherit `ICMG_RUN_MODE`** → premium assumed present → local dormant.
   **Severity: Minor** (safe default, no regression, no runaway inference). Mitigation:
   cron-runner must set env on child spawn + wiring test verifies.
2. **`agent --exec` has API key but user wants local** → premium wins, local off.
   **Severity: Minor** — closed via `explicit_local` (`--local` flag).
3. **PreCompact selected but rarely no-premium** → fires local only in daemon-compact.
   **Severity: Minor**, documented; correctly gated.
4. **`session-disable` per-process in cron** → cold-fail re-attempts each night (no persist).
   **Severity: Minor**, accepted; persist-to-file deferred.
5. **Local 8b output quality on `agent`** → low vs Claude. **Severity: Minor** — gated to
   no-premium/explicit only; advisory-only (no auto-edit). User sees it only when Claude is
   genuinely absent or explicitly chosen.
6. **`agent --local --exec` temptation** → weak model auto-editing files. **Severity: was
   Critical → dissolved**: hard-refused by the advisory-only rule. `--exec` needs premium CLI.

No Critical failure modes remain — `premium_available` dissolved agent-with-key-forcing-local;
advisory-only dissolved weak-model-auto-edit.
